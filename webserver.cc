#include "webserver.h"

WebServer::WebServer(unsigned short port, size_t num_threads=1) : endpoint(ip::tcp::v4(), port), 
    acceptor(m_io_service, endpoint), num_threads(num_threads) {}

void WebServer::run() {
    do_accept();

    //If num_threads>1, start m_io_service.run() in (num_threads-1) threads for thread-pooling
    for(size_t c=1;c<num_threads;c++) {
        threads.emplace_back([this](){
            m_io_service.run();
        });
    }

    //Main thread
    m_io_service.run();

    //Wait for the rest of the threads, if any, to finish as well
    for(thread& t: threads) {
        t.join();
    }
}

void WebServer::do_accept() {
    //Create new socket for this connection
    //Shared_ptr is used to pass temporary objects to the asynchronous functions
    shared_ptr<ip::tcp::socket> socket(new ip::tcp::socket(m_io_service));

    acceptor.async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        //Immediately start accepting a new connection
        do_accept();

        if(!ec) {
            process_request_and_respond(socket);
        }
    });
}

void WebServer::process_request_and_respond(shared_ptr<ip::tcp::socket> socket) {
    //Create new read_buffer for async_read_until()
    //Shared_ptr is used to pass temporary objects to the asynchronous functions
    shared_ptr<boost::asio::streambuf> read_buffer(new boost::asio::streambuf);

    async_read_until(*socket, *read_buffer, "\r\n\r\n",
    [this, socket, read_buffer](const boost::system::error_code& ec, size_t bytes_transferred) {
        if(!ec) {
            //read_buffer->size() is not necessarily the same as bytes_transferred, from Boost-docs:
            //"After a successful async_read_until operation, the streambuf may contain additional data beyond the delimiter"
            //The chosen solution is to extract lines from the stream directly when parsing the header. What is left of the
            //read_buffer (maybe some bytes of the content) is appended to in the async_read-function below (for retrieving content).
            size_t total=read_buffer->size();

            //Convert to istream to extract string-lines
            istream stream(read_buffer.get());

            shared_ptr<Request> request(new Request());
            *request=parse_request(stream);

            size_t num_additional_bytes=total-bytes_transferred;

            //If content, read that as well
            if(request->headers.count("Content-Length")>0) {
                async_read(*socket, *read_buffer, transfer_exactly(stoull(request->headers["Content-Length"])-num_additional_bytes), 
                [this, socket, read_buffer, request](const boost::system::error_code& ec, size_t bytes_transferred) {
                    if(!ec) {
                        //Store pointer to read_buffer as istream object
                        request->content=shared_ptr<istream>(new istream(read_buffer.get()));

                        do_reply(socket, request);
                    }
                });
            }
            else {                   
                do_reply(socket, request);
            }
        }
    });
}

Request WebServer::parse_request(istream& stream) {
    Request request;

    boost::regex e("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");

    boost::smatch sm;

    //First parse request method, path, and HTTP-version from the first line
    string line;
    getline(stream, line);
    line.pop_back();
    if(regex_match(line, sm, e)) {        
        request.method=sm[1];
        request.path=sm[2];
        request.http_version=sm[3];

        bool matched;
        e="^([^:]*): ?(.*)$";
        //Parse the rest of the header
        do {
            getline(stream, line);
            line.pop_back();
            matched=regex_match(line, sm, e);
            if(matched) {
                request.headers[sm[1]]=sm[2];
            }

        } while(matched==true);
    }

    return request;
}

void WebServer::do_reply(shared_ptr<ip::tcp::socket> socket, shared_ptr<Request> request) {
    //Find path- and method-match, and generate response
    for(auto& res: resources) {
        boost::regex e(res.first);
        boost::smatch sm_res;
        if(regex_match(request->path, sm_res, e)) {
            if(res.second.count(request->method)>0) {
                shared_ptr<boost::asio::streambuf> write_buffer(new boost::asio::streambuf);
                ostream response(write_buffer.get());
                res.second[request->method](response, *request, sm_res);

                //Capture write_buffer in lambda so it is not destroyed before async_write is finished
                async_write(*socket, *write_buffer, [this, socket, request, write_buffer](const boost::system::error_code& ec, size_t bytes_transferred) {
                    //HTTP persistent connection (HTTP 1.1):
                    if(!ec && stof(request->http_version)>1.05)
                        process_request_and_respond(socket);
                });
                return;
            }
        }
    }
}