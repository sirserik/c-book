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

CXXFLAGS := $(CXXSTD) $(WARNINGS) $(OPT) -Iinclude -MMD -MP
LDFLAGS  :=

SRCS     := $(filter-out src/main.cpp, $(wildcard src/*.cpp))
OBJS     := $(patsubst src/%.cpp, build/%.o, $(SRCS))
DEPS     := $(OBJS:.o=.d)

TEST_SRCS := $(wildcard tests/*.cpp)
TEST_OBJS := $(patsubst tests/%.cpp, build/tests/%.o, $(TEST_SRCS))
TEST_DEPS := $(TEST_OBJS:.o=.d)

UTIL_SRCS := $(wildcard utils/*.cpp)
UTILS     := $(patsubst utils/%.cpp, build/%, $(UTIL_SRCS))

all: build/mydb utils

utils: $(UTILS)

build/mydb: build/main.o $(OBJS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: $@ ($(MODE))"

build/%: utils/%.cpp $(OBJS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/main.o: src/main.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

tests: $(TEST_OBJS:.o=)

build/tests/%: build/tests/%.o $(OBJS)
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/tests/%.o: tests/%.cpp
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build

-include $(DEPS)
-include $(TEST_DEPS)

.PHONY: all utils tests clean
