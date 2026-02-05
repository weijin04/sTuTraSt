# Makefile for TuTraSt C++ implementation
# Simple wrapper around CMake for convenience

.PHONY: all build clean install test help

BUILD_DIR = build
BINARY = $(BUILD_DIR)/tutrast

all: build

# Build the project
build:
	@echo "Building TuTraSt..."
	@./build.sh

# Build in debug mode
debug:
	@echo "Building TuTraSt (Debug mode)..."
	@./build.sh debug

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@./build.sh clean
	@rm -f *.dat *.out msd*.dat

# Install system-wide (requires sudo)
install: build
	@echo "Installing TuTraSt system-wide..."
	@cd $(BUILD_DIR) && sudo make install
	@echo "Installation complete. You can now run 'tutrast' from anywhere."

# Run a quick test
test: build
	@echo "Running test..."
	@if [ -f grid.cube ] && [ -f input.param ]; then \
		./$(BINARY); \
	else \
		echo "Error: grid.cube or input.param not found"; \
		exit 1; \
	fi

# Run with timing
benchmark: build
	@echo "Running benchmark..."
	@time ./$(BINARY)

# Display help
help:
	@echo "TuTraSt C++ Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make           - Build the project (release mode)"
	@echo "  make build     - Build the project (release mode)"
	@echo "  make debug     - Build the project (debug mode)"
	@echo "  make clean     - Clean build artifacts and output files"
	@echo "  make install   - Install system-wide (requires sudo)"
	@echo "  make test      - Build and run a test"
	@echo "  make benchmark - Build and run with timing"
	@echo "  make help      - Display this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - CMake 3.10+"
	@echo "  - GCC 7.0+ or Clang 6.0+"
	@echo "  - Linux operating system"
