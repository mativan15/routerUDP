CXX      := c++
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread

all: server client

server: server.cpp protocol.h
	$(CXX) $(CXXFLAGS) -o $@ $<

client: client.cpp protocol.h
	$(CXX) $(CXXFLAGS) -o $@ $<

# Run in separate terminals: one for server, one for client.
run-server: server
	./server

run-client: client
	./client

clean:
	rm -f server client

.PHONY: all run-server run-client clean
