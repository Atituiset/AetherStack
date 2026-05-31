.PHONY: all build test clean

all: build

build:
	@mkdir -p build
	@cd build && cmake .. && $(MAKE)

test: build
	@cd build && ctest --output-on-failure

clean:
	@rm -rf build
