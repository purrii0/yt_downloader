# Simple build:  make        -> ./ytpl
#                make test   -> runs the selection parser tests
#                make clean
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDLIBS    = -lncursesw -pthread

SRC = src/main.cpp src/app.cpp src/cli.cpp src/tui.cpp src/tracker.cpp \
      src/ytdlp.cpp src/select.cpp src/util.cpp src/downloader.cpp src/config.cpp
HDR = $(wildcard src/*.hpp)

ytpl: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDLIBS)

select_test: tests/select_test.cpp src/select.cpp src/select.hpp
	$(CXX) $(CXXFLAGS) -Isrc tests/select_test.cpp src/select.cpp -o $@

test: select_test
	./select_test

clean:
	rm -f ytpl select_test

.PHONY: test clean
