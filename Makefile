CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Isrc -Iffi/spec-bridge/include

SRC = src/main.cpp \
      src/ast/extractor.cpp \
      src/ast/skeletonizer.cpp \
      src/graph/symbol_table.cpp \
      src/graph/graph.cpp \
      src/router/router.cpp \
      src/spec/stitcher.cpp \
      src/spec/linter.cpp

FFI_DIR = ffi/spec-bridge
FFI_LIB = $(FFI_DIR)/target/release/libsyntra_spec_bridge.a
LDFLAGS ?= $(FFI_LIB) -lpthread -ldl

TARGET = bin/lgcc

all: $(TARGET)

$(FFI_LIB): $(FFI_DIR)/src/lib.rs $(FFI_DIR)/Cargo.toml
	cargo build --release --offline --manifest-path $(FFI_DIR)/Cargo.toml

$(TARGET): $(SRC) $(FFI_LIB)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC) $(LDFLAGS) -o $@

clean:
	rm -rf bin/
	cargo clean --manifest-path $(FFI_DIR)/Cargo.toml

.PHONY: all clean

