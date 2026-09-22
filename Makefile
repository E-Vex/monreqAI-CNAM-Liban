# monreqAI-CNAM-Liban (C edition) - GNU Makefile

PROJECT := monreqai_c
VERSION := 2.0.0
TARGET  := isae_monitor

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -O2 \
            -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
            -DISAE_VERSION_STRING=\"$(VERSION)\"
DEBUG_FLAGS := -g -O0 -DDEBUG -fsanitize=address,undefined
INCLUDES := -Iinclude -Ithird_party/cjson

# System dependencies (kept; libcjson is vendored, see third_party/cjson)
PKG_CONFIG  := pkg-config
CURL_CFLAGS := $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
CURL_LIBS   := $(shell $(PKG_CONFIG) --libs   libcurl 2>/dev/null)
LIBXML2_CFLAGS := $(shell $(PKG_CONFIG) --cflags libxml-2.0 2>/dev/null)
LIBXML2_LIBS   := $(shell $(PKG_CONFIG) --libs   libxml-2.0 2>/dev/null)

ALL_CFLAGS := $(CFLAGS) $(INCLUDES) $(CURL_CFLAGS) $(LIBXML2_CFLAGS)
ALL_LIBS   := $(CURL_LIBS) $(LIBXML2_LIBS) -lm

# Source files
SRCS := \
    src/main.c \
    src/departments.c \
    src/models.c \
    src/config.c \
    src/state.c \
    src/httpclient.c \
    src/feed.c \
    src/providers.c \
    src/keywords.c \
    src/classifier.c \
    src/telegram.c \
    src/pipeline.c \
    src/dotenv.c

# Vendored third-party sources (compiled once into build/third_party/)
VENDOR_SRCS := third_party/cjson/cJSON.c

OBJS       := $(SRCS:src/%.c=build/%.o)
VENDOR_OBJS := $(VENDOR_SRCS:third_party/%.c=build/third_party/%.o)

# Default target
all: $(TARGET)

# Create build directory tree
build build/third_party build/third_party/cjson:
	mkdir -p $@

# Link executable
$(TARGET): $(OBJS) $(VENDOR_OBJS)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJS) $(VENDOR_OBJS) $(ALL_LIBS)

# Compile project sources
build/%.o: src/%.c | build
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Compile vendored cJSON
build/third_party/cjson/%.o: third_party/cjson/%.c | build/third_party/cjson
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Generic vendored source rule (for any future third-party packages)
build/third_party/%.o: third_party/%.c | build/third_party
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Debug build with ASan/UBSan.
# NOTE: appends to ALL_CFLAGS (file origin), NOT CFLAGS: CFLAGS is defined
# with `?=` which gives it "default" origin, and target-specific `+=` on a
# default-origin variable is silently ignored by GNU make -- the old
# `debug: CFLAGS += $(DEBUG_FLAGS)` produced a plain -O2 binary with NO
# sanitizers while claiming to be instrumented.
debug: ALL_CFLAGS += $(DEBUG_FLAGS)
debug: clean $(TARGET)

# Clean build artifacts
clean:
	rm -rf build $(TARGET) tests/*.out tests/state.* tests/*.json

# Install
install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 0755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)

# Uninstall
uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)

# Memory check (one-shot run; see tests/ for full parity tests)
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
	    ./$(TARGET) --once 2>/dev/null || \
	    valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
	        ./$(TARGET) --dry-run

# Run the test suite
test: $(TARGET)
	@cd tests && ./test_parity.sh

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@$(PKG_CONFIG) --exists libcurl   && echo "[ok] libcurl   $(shell $(PKG_CONFIG) --modversion libcurl)"   || echo "[!!] libcurl   not found"
	@$(PKG_CONFIG) --exists libxml-2.0 && echo "[ok] libxml2   $(shell $(PKG_CONFIG) --modversion libxml-2.0)" || echo "[!!] libxml2   not found"
	@echo "[ok] cJSON     vendored (v1.7.18)"

# Help
help:
	@echo "$(PROJECT) v$(VERSION) - GNU Make targets:"
	@echo "  all        - Build the project (default)"
	@echo "  debug      - Build with -g -O0 -fsanitize=address,undefined"
	@echo "  clean      - Remove build artifacts and test outputs"
	@echo "  install    - Install to \$$DESTDIR/usr/local/bin (default /usr/local/bin)"
	@echo "  uninstall  - Remove from \$$DESTDIR/usr/local/bin"
	@echo "  valgrind   - Run one-shot under valgrind (no leaks expected)"
	@echo "  test       - Run tests/test_parity.sh"
	@echo "  check-deps - Verify libcurl/libxml2 are available"
	@echo "  help       - Show this help"

.PHONY: all debug clean install uninstall valgrind test check-deps help
