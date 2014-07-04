all: build/file_server build/file_client

build/file_server: build examples/file_server.cpp
	g++ -g --std=c++11 -I./include examples/file_server.cpp -libverbs -o build/file_server

build/file_client: build examples/file_client.cpp
	g++ -g --std=c++11 -I./include examples/file_client.cpp -libverbs -o build/file_client

build:
	mkdir build

clean:
	rm -rf build
