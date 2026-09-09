CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread -MMD -MP -O2

SERVER_SRC = $(wildcard server/*.cpp)
CLIENT_SRC = $(wildcard client/*.cpp)

SERVER_OBJ = $(patsubst server/%.cpp, build/server/%.o, $(SERVER_SRC))
CLIENT_OBJ = $(patsubst client/%.cpp, build/client/%.o, $(CLIENT_SRC))

SERVER = redis-server
CLIENT = redis-client

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

build/server/%.o: server/%.cpp
	mkdir -p build/server
	$(CXX) $(CXXFLAGS) -Iserver -c $< -o $@

$(CLIENT): $(CLIENT_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

build/client/%.o: client/%.cpp
	mkdir -p build/client
	$(CXX) $(CXXFLAGS) -Iclient -c $< -o $@

-include $(SERVER_OBJ:.o=.d)
-include $(CLIENT_OBJ:.o=.d)

clean:
	rm -rf build $(SERVER) $(CLIENT)

run-server: $(SERVER)
	./$(SERVER)

run-client: $(CLIENT)
	./$(CLIENT)