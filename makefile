CXXFLAGS = -std=c++17 -Wall -pthread -MMD -MP -O2

SRCS := $(wildcard source/*.cpp)
OBJS := $(patsubst source/%.cpp, build/%.o, $(SRCS))

all: final_server

build:
	mkdir -p build

build/%.o: source/%.cpp | build
	g++ $(CXXFLAGS) -c $< -o $@

final_server: $(OBJS)
	g++ $(CXXFLAGS) $(OBJS) -o final_server

clean:
	rm -rf build final_server

rebuild: clean all

run: all
	./final_server