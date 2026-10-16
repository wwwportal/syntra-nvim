CARGO ?= cargo
TARGET = bin/lgcc

all: $(TARGET)

$(TARGET): $(shell find crates/lgcc/src -type f) crates/lgcc/Cargo.toml
	@mkdir -p bin
	$(CARGO) build --release --manifest-path crates/lgcc/Cargo.toml
	cp crates/lgcc/target/release/lgcc $(TARGET)

clean:
	rm -rf bin/
	$(CARGO) clean --manifest-path crates/lgcc/Cargo.toml

.PHONY: all clean


