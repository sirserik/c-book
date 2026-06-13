CXX      := g++
CXXSTD   := -std=c++17
WARNINGS := -Wall -Wextra -Wpedantic
CXXFLAGS := $(CXXSTD) $(WARNINGS) -g -O0

UTIL_SRCS := $(wildcard utils/*.cpp)
UTILS     := $(patsubst utils/%.cpp, build/%, $(UTIL_SRCS))

all: $(UTILS)

build/%: utils/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -rf build

.PHONY: all clean
