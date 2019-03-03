all:httpserver upload
httpserver:httpserver.cpp utils.hpp threadpool.hpp
	g++ -o $@ $^ -lpthread -std=c++11

upload:upload.cpp utils.hpp
	g++ -o $@ $^ -std=c++11

.PHONY:clean
clean:
	rm -rf httpserver upload

