# Makefile для demo-rpg
#
# Цели:
#   make           — собрать игру в build/rpg
#   make tests     — собрать тесты в build/tests
#   make run       — запустить игру
#   make clean     — удалить всё собранное
#
# Главная фишка — флаг -MMD: компилятор сам генерирует .d-файлы со списком
# зависимостей каждого .cpp от .h. Мы их подключаем, и make сам пересобирает
# нужные .cpp при изменении заголовка. Никаких ручных правок при добавлении
# include — система сама всё отслеживает.

CXX      := g++
CXXSTD   := -std=c++11
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
DEBUG    := -g -O0
RELEASE  := -O2 -DNDEBUG

# Режим сборки. По умолчанию debug. Переключение: `make MODE=release`
MODE     ?= debug
ifeq ($(MODE),release)
    OPT := $(RELEASE)
else
    OPT := $(DEBUG)
endif

CXXFLAGS := $(CXXSTD) $(WARNINGS) $(OPT) -Iinclude -MMD -MP
LDFLAGS  :=

# Список исходников. wildcard — список всех .cpp в src/ кроме main.cpp
SRCS     := $(filter-out src/main.cpp, $(wildcard src/*.cpp))
OBJS     := $(patsubst src/%.cpp, build/%.o, $(SRCS))
DEPS     := $(OBJS:.o=.d)

# Тесты — все .cpp в tests/
TEST_SRCS := $(wildcard tests/*.cpp)
TEST_OBJS := $(patsubst tests/%.cpp, build/tests/%.o, $(TEST_SRCS))
TEST_DEPS := $(TEST_OBJS:.o=.d)

# Главная цель — игра
all: build/rpg

build/rpg: build/main.o $(OBJS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: $@ ($(MODE))"

build/main.o: src/main.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Тесты собираются отдельно — каждый .cpp в tests/ это отдельный бинарник.
# (В книге мы перейдём на GoogleTest, пока — простой self-test.)
tests: $(TEST_OBJS:.o=)

build/tests/%: build/tests/%.o $(OBJS)
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/tests/%.o: tests/%.cpp
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: build/rpg
	./build/rpg

clean:
	rm -rf build

# Подключаем .d-файлы, если есть. Они содержат правила вида:
#   build/foo.o: src/foo.cpp include/bar.h
# Так make узнаёт, что foo.o нужно пересобрать при изменении bar.h.
-include $(DEPS)
-include $(TEST_DEPS)

.PHONY: all tests run clean
