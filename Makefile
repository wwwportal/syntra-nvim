CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Isrc

SRC = src/main.cpp \
      src/ast/extractor.cpp \
      src/ast/skeletonizer.cpp \
      src/graph/symbol_table.cpp \
      src/graph/graph.cpp \
      src/router/router.cpp \
      src/spec/stitcher.cpp \
      src/spec/linter.cpp

TARGET = bin/lgcc

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

clean:
	rm -rf bin/

.PHONY: all clean
