#include "webserver.h"

WebServer::WebServer(NginxConfig config, unsigned short port, size_t num_threads=1) 
    : endpoint(ip::tcp::v4(), port), acceptor(m_io_service, endpoint), num_threads(num_threads)
    {
        // initialize echo hadler
        auto paths1 = make_shared<unordered_set<string>>();
        echo_handler = make_shared<RequestHandlerEcho>(paths1);
        echo_handler->paths->insert("/");

        // initialize static handler
        auto paths2 = make_shared<unordered_map<string, string>>();
        static_handler = make_shared<RequestHandlerStatic>(paths2); 

        extract(config);
    }

void WebServer::run() {
    do_accept();

    //If num_threads>1, start m_io_service.run() in (num_threads-1) threads for thread-pooling
    for(size_t c=1;c<num_threads;c++) {
        threads.emplace_back([this](){ m_io_service.run();});
    }

    //Main thread
    m_io_service.run();

    //Wait for the rest of the threads, if any, to finish as well
    for(thread& t: threads) { t.join(); }
}

void WebServer::do_accept() {
    //Create new socket for this connection
    //Shared_ptr is used to pass temporary objects to the asynchronous functions
    shared_ptr<ip::tcp::socket> socket(new ip::tcp::socket(m_io_service));

    acceptor.async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        //Immediately start accepting a new connection
        do_accept();

        if(!ec) { process_request(socket); }
    });
}

void WebServer::process_request(shared_ptr<ip::tcp::socket> socket) {
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
            else { do_reply(socket, request);}
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
    shared_ptr<boost::asio::streambuf> write_buffer(new boost::asio::streambuf);
    ostream response(write_buffer.get());
    if (echo_handler->paths->find(request->path) != echo_handler->paths->end()) {
        echo_handler->get_response(response, *request);
    }
    else {
        static_handler->get_response(response, *request);
    }
    //Capture write_buffer in lambda so it is not destroyed before async_write is finished
    async_write(*socket, *write_buffer, [this, socket, request, write_buffer](const boost::system::error_code& ec, size_t bytes_transferred) {
        //HTTP persistent connection (HTTP 1.1):
        if(!ec && stof(request->http_version)>1.05)
            process_request(socket);
    });
    return;
}

unsigned short extract_port(NginxConfig config) {
  // Initialize variables
  string key = "";
  string value = "";

  for (size_t i = 0; i < config.statements_.size(); i++) {
    //search in child block
    if (config.statements_[i]->child_block_ != nullptr) {
        extract_port(*(config.statements_[i]->child_block_));
    }

    if (config.statements_[i]->tokens_.size() >= 1) {
      key = config.statements_[i]->tokens_[0];
    }

    if (config.statements_[i]->tokens_.size() >= 2) {
      value = config.statements_[i]->tokens_[1];
    }

    if (key == "listen" && value != "") {
      return atoi(value.c_str());
    }
  }

  return 8080;
}

void WebServer::extract(NginxConfig config) {
  // Initialize variables
  string key = "";
  string value = "";

  for (size_t i = 0; i < config.statements_.size(); i++) {
    //search in child block
    if (config.statements_[i]->child_block_ != nullptr) {
      if(config.statements_[i]->tokens_[0] == "location"){
        extract_location(*(config.statements_[i]->child_block_), config.statements_[i]->tokens_[1]);
      }
      else{
        extract(*(config.statements_[i]->child_block_));
      }
    }
  }
}

void WebServer::extract_location(NginxConfig config, string path){
    // Initialize variables
    string key = "";
    string value = "";

    for (size_t i=0; i < config.statements_.size(); i++){
      if (config.statements_[i]->child_block_ != nullptr) {
            extract_location(*(config.statements_[i]->child_block_), path);
      }

      if (config.statements_[i]->tokens_.size() >= 1) {
          key = config.statements_[i]->tokens_[0];
      }

      if (config.statements_[i]->tokens_.size() >= 2) {
          value = config.statements_[i]->tokens_[1];
      }

      if (key == "root" && value != "") {
          if(path == "echo"){
            echo_handler->paths->insert(value);
          }
          else{
            (*(static_handler->paths))[path] = value;
          }
      }
    }
}