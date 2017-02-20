CC = g++
FLAGS = -std=c++11 -Wall -Werror -g
GTEST_DIR=googletest/googletest

run: webserver

all: webserver configparser configparser_test webserver_test

config_parser: config_parser.o config_parser_main.o 
	$(CC) $(FLAGS) $^ -o $@

config_parser_test: config_parser.o 
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -I${GTEST_DIR} -pthread -c ${GTEST_DIR}/src/gtest-all.cc
	ar -rv libgtest.a gtest-all.o
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -pthread $^ config_parser_test.cc ${GTEST_DIR}/src/gtest_main.cc libgtest.a -o config_parser_test

webserver: config_parser.o webserver.o webserver_main.o request.o response.o echo_handler.o static_handler.o not_found_handler.o
	$(CC) $(FLAGS) $^ -o webserver -lboost_system -lboost_regex

webserver_test: webserver.o
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -I${GTEST_DIR} -pthread -c ${GTEST_DIR}/src/gtest-all.cc
	ar -rv libgtest.a gtest-all.o
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -pthread $^ webserver_test.cc config_parser.cc ${GTEST_DIR}/src/gtest_main.cc libgtest.a -o webserver_test -lboost_system -lboost_regex

%.o: %.cc
	$(CC) $(FLAGS) -c $<

integration_test: webserver
	python3 integration_test.py

unit_test_coverage:
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -I${GTEST_DIR} -pthread -c ${GTEST_DIR}/src/gtest-all.cc
	ar -rv libgtest.a gtest-all.o
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -pthread $^ request_test.cc request.cc ${GTEST_DIR}/src/gtest_main.cc libgtest.a -o request_test -lboost_system -lboost_regex  -fprofile-arcs -ftest-coverage
	./request_test 
	# gcov -r request.cc
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -pthread $^ response_test.cc response.cc ${GTEST_DIR}/src/gtest_main.cc libgtest.a -o response_test -fprofile-arcs -ftest-coverage
	./response_test 
	# gcov -r response.cc
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -pthread $^ echo_handler_test.cc request.cc response.cc echo_handler.cc ${GTEST_DIR}/src/gtest_main.cc libgtest.a -o echo_handler_test -lboost_system -lboost_regex  -fprofile-arcs -ftest-coverage
	./echo_handler_test 
	# gcov -r echo_handler.cc

	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -pthread $^ webserver_test.cc webserver.cc config_parser.cc response.cc ${GTEST_DIR}/src/gtest_main.cc libgtest.a -o webserver_test -lboost_system -lboost_regex  -fprofile-arcs -ftest-coverage
	./webserver_test
	# gcov -r webserver.cc

clean:
	rm -f *.o *.a *.gcno *.gcov *.gcda config_parser webserver *_test

.PHONY: clean run all integration_test unit_test_coverage
