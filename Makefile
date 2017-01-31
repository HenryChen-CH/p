CC = g++
FLAGS = -std=c++11 -Wall -Werror -g
GTEST_DIR=googletest/googletest

run: webserver

all: webserver configparser configparser_test

config_parser: config_parser.o config_parser_main.o 
	$(CC) $(FLAGS) $^ -o $@

config_parser_test: config_parser.o 
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -I${GTEST_DIR} -pthread -c ${GTEST_DIR}/src/gtest-all.cc
	ar -rv libgtest.a gtest-all.o
	$(CC) $(FLAGS) -isystem ${GTEST_DIR}/include -pthread $^ config_parser_test.cc ${GTEST_DIR}/src/gtest_main.cc libgtest.a -o config_parser_test

webserver: config_parser.o webserver.o webserver_main.o
	$(CC) $(FLAGS) $^ -o webserver -lboost_system -lboost_regex

%.o: %.cc
	$(CC) $(FLAGS) -c $<

clean:
	rm -f *.o *.a webserver config_parser config_parser_test

.PHONY: clean
