# ISAE Monitor C - GNU Makefile

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
DEBUG_FLAGS = -g -O0 -DDEBUG
INCLUDES = -Iinclude

# pkg-config for dependencies
PKG_CONFIG = pkg-config
CURL_CFLAGS = $(shell $(PKG_CONFIG) --cflags libcurl)
CURL_LIBS = $(shell $(PKG_CONFIG) --libs libcurl)
LIBXML2_CFLAGS = $(shell $(PKG_CONFIG) --cflags libxml-2.0)
LIBXML2_LIBS = $(shell $(PKG_CONFIG) --libs libxml-2.0)
CJSON_CFLAGS = $(shell $(PKG_CONFIG) --cflags libcjson)
CJSON_LIBS = $(shell $(PKG_CONFIG) --libs libcjson)

ALL_CFLAGS = $(CFLAGS) $(INCLUDES) $(CURL_CFLAGS) $(LIBXML2_CFLAGS) $(CJSON_CFLAGS)
ALL_LIBS = $(CURL_LIBS) $(LIBXML2_LIBS) $(CJSON_LIBS) -lm

# Source files
SRCS = src/main.c \
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
       src/pipeline.c

OBJS = $(SRCS:src/%.c=build/%.o)
TARGET = isae_monitor

# Default target
all: $(TARGET)

# Create build directory
build:
	mkdir -p build

# Link executable
$(TARGET): $(OBJS)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJS) $(ALL_LIBS)

# Compile source files
build/%.o: src/%.c | build
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Debug build
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean $(TARGET)

# Clean build artifacts
clean:
	rm -rf build $(TARGET)

# Install
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# Uninstall
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Run valgrind memory check (requires test setup)
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET) --once

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@$(PKG_CONFIG) --exists libcurl && echo "✓ libcurl found" || echo "✗ libcurl not found"
	@$(PKG_CONFIG) --exists libxml-2.0 && echo "✓ libxml2 found" || echo "✗ libxml2 not found"
	@$(PKG_CONFIG) --exists libcjson && echo "✓ libcjson found" || echo "✗ libcjson not found"

# Help
help:
	@echo "ISAE Monitor C - Makefile Targets:"
	@echo "  all        - Build the project (default)"
	@echo "  debug      - Build with debug symbols"
	@echo "  clean      - Remove build artifacts"
	@echo "  install    - Install to /usr/local/bin"
	@echo "  uninstall  - Remove from /usr/local/bin"
	@echo "  valgrind   - Run memory leak check"
	@echo "  check-deps - Verify dependencies"
	@echo "  help       - Show this help"

.PHONY: all debug clean install uninstall valgrind check-deps help
