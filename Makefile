CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -fsanitize=address,undefined -g -Iinclude

SRCDIR = src
INCDIR = include
TESTDIR = tests
BUILDDIR = build

OBJS = $(BUILDDIR)/crc16.o $(BUILDDIR)/protocol.o $(BUILDDIR)/parser.o $(BUILDDIR)/vdev.o

all: test

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/test_protocol: $(BUILDDIR)/crc16.o $(BUILDDIR)/protocol.o $(TESTDIR)/test_protocol.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILDDIR)/test_streaming: $(OBJS) $(TESTDIR)/test_streaming.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

test: $(BUILDDIR)/test_protocol $(BUILDDIR)/test_streaming
	./$(BUILDDIR)/test_protocol
	./$(BUILDDIR)/test_streaming

clean:
	rm -rf $(BUILDDIR)

compile_commands.json:
	@printf '[\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/crc16.c -o build/crc16.o",\n    "file": "src/crc16.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/protocol.c -o build/protocol.o",\n    "file": "src/protocol.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/parser.c -o build/parser.o",\n    "file": "src/parser.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/vdev.c -o build/vdev.o",\n    "file": "src/vdev.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c tests/test_protocol.c -o build/test_protocol.o",\n    "file": "tests/test_protocol.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c tests/test_streaming.c -o build/test_streaming.o",\n    "file": "tests/test_streaming.c"\n  }\n]\n' "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" > compile_commands.json

.PHONY: all test clean compile_commands.json
