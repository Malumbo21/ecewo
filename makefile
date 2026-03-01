.PHONY: test sanitizer format format-file lint lint-fix lint-file help

SOURCES := $(shell find src src/utils include -type f \( -name "*.c" -o -name "*.h" \))

test:
	@mkdir -p build
	@cmake -B build -DECEWO_BUILD_TESTS=ON
	@cmake --build build -j$(nproc)
	@ctest --test-dir build

sanitizer:
	@mkdir -p build-sanitizer
	@( \
		CC=clang cmake -B build-sanitizer \
			-DCMAKE_BUILD_TYPE=Debug \
			-DECEWO_BUILD_TESTS=ON \
			-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
			-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" && \
		cmake --build build-sanitizer -j$(nproc) && \
		ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1" \
		UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
		ctest --test-dir build-sanitizer --output-on-failure --verbose \
	)

format:
	@clang-format -i $(SOURCES)

format-file:
	@clang-format -i $(FILE)

lint:
	@echo "Running clang-tidy on $(words $(SOURCES)) files..."
	@clang-tidy -p build $(SOURCES) --quiet 2>&1 | grep -v "warnings generated" || true

lint-fix:
	@echo "Running clang-tidy with auto-fix..."
	@clang-tidy -p build $(SOURCES) --fix-errors

lint-file:
	@test -n "$(FILE)" || (echo "Usage: make lint-file FILE=path/to/file.c" && exit 1)
	@clang-tidy -p build $(FILE)

help:
	@echo "Available targets:"
	@echo "  make test"			- Build and run tests
	@echo "  make sanitizer"    - Build and run tests with sanitizers
	@echo "  make format        - Run clang-format"
	@echo "  make format-file FILE=src/file.c - Format single file"
	@echo "  make lint          - Run clang-tidy (quiet mode, recommended)"
	@echo "  make lint-fix      - Auto-fix issues where possible"
	@echo "  make lint-file FILE=src/file.c - Check single file"
