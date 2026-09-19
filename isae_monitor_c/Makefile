# ISAE Monitor C - Makefile
# Pure C11 implementation with libcurl and libxml2

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 $(shell pkg-config --cflags libcurl libcjson libxml-2.0)
LDFLAGS = -lcurl -lxml2 -lm

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build

# Source files
SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Target executable
TARGET = isae_monitor

# Default target
all: $(BUILDDIR) $(TARGET)

# Create build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Link executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c -o $@ $<

# Clean build artifacts
clean:
	rm -rf $(BUILDDIR) $(TARGET)

# Install to system
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# Uninstall
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: clean all

# Run with valgrind (memory check)
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET) --help

# Dependencies (auto-generated would be better, but this works for small projects)
$(BUILDDIR)/main.o: $(SRCDIR)/main.c $(INCDIR)/isae_monitor/config.h $(INCDIR)/isae_monitor/state.h $(INCDIR)/isae_monitor/pipeline.h $(INCDIR)/isae_monitor/common.h
$(BUILDDIR)/departments.o: $(SRCDIR)/departments.c $(INCDIR)/isae_monitor/departments.h $(INCDIR)/isae_monitor/common.h
$(BUILDDIR)/models.o: $(SRCDIR)/models.c $(INCDIR)/isae_monitor/models.h $(INCDIR)/isae_monitor/common.h
$(BUILDDIR)/config.o: $(SRCDIR)/config.c $(INCDIR)/isae_monitor/config.h $(INCDIR)/isae_monitor/common.h
$(BUILDDIR)/state.o: $(SRCDIR)/state.c $(INCDIR)/isae_monitor/state.h $(INCDIR)/isae_monitor/common.h
$(BUILDDIR)/httpclient.o: $(SRCDIR)/httpclient.c $(INCDIR)/isae_monitor/httpclient.h $(INCDIR)/isae_monitor/common.h
$(BUILDDIR)/feed.o: $(SRCDIR)/feed.c $(INCDIR)/isae_monitor/feed.h $(INCDIR)/isae_monitor/httpclient.h $(INCDIR)/isae_monitor/models.h
$(BUILDDIR)/providers.o: $(SRCDIR)/providers.c $(INCDIR)/isae_monitor/providers.h $(INCDIR)/isae_monitor/httpclient.h
$(BUILDDIR)/keywords.o: $(SRCDIR)/keywords.c $(INCDIR)/isae_monitor/keywords.h $(INCDIR)/isae_monitor/departments.h
$(BUILDDIR)/classifier.o: $(SRCDIR)/classifier.c $(INCDIR)/isae_monitor/classifier.h $(INCDIR)/isae_monitor/providers.h $(INCDIR)/isae_monitor/keywords.h
$(BUILDDIR)/telegram.o: $(SRCDIR)/telegram.c $(INCDIR)/isae_monitor/telegram.h $(INCDIR)/isae_monitor/httpclient.h
$(BUILDDIR)/pipeline.o: $(SRCDIR)/pipeline.c $(INCDIR)/isae_monitor/pipeline.h $(INCDIR)/isae_monitor/feed.h $(INCDIR)/isae_monitor/classifier.h $(INCDIR)/isae_monitor/telegram.h

.PHONY: all clean install uninstall debug valgrind
