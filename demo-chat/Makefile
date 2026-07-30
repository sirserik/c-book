CXX      := g++
CXXSTD   := -std=c++11
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow
DEBUG    := -g -O0
RELEASE  := -O2 -DNDEBUG

MODE     ?= debug
ifeq ($(MODE),release)
    OPT := $(RELEASE)
else
    OPT := $(DEBUG)
endif

CXXFLAGS := $(CXXSTD) $(WARNINGS) $(OPT) -Iinclude -MMD -MP -pthread
LDFLAGS  := -pthread

SRCS     := $(filter-out src/main.cpp, $(wildcard src/*.cpp))
OBJS     := $(patsubst src/%.cpp, build/%.o, $(SRCS))
DEPS     := $(OBJS:.o=.d)

UTIL_SRCS := $(wildcard utils/*.cpp)
UTILS     := $(patsubst utils/%.cpp, build/%, $(UTIL_SRCS))

TEST_SRCS := $(wildcard tests/*.cpp)
TESTS     := $(patsubst tests/%.cpp, build/tests/%, $(TEST_SRCS))

all: utils

utils: $(UTILS)

tests: $(TESTS)

build/tests/%: tests/%.cpp $(OBJS)
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/%: utils/%.cpp $(OBJS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build

-include $(DEPS)

.PHONY: all utils tests clean
