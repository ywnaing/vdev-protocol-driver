CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -fsanitize=address,undefined -g -Iinclude

SRCDIR = src
INCDIR = include
TESTDIR = tests
BUILDDIR = build

OBJS = $(BUILDDIR)/crc16.o $(BUILDDIR)/protocol.o $(BUILDDIR)/parser.o $(BUILDDIR)/vdev.o $(BUILDDIR)/driver.o

all: test

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/test_protocol: $(BUILDDIR)/crc16.o $(BUILDDIR)/protocol.o $(TESTDIR)/test_protocol.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILDDIR)/test_streaming: $(OBJS) $(TESTDIR)/test_streaming.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILDDIR)/test_vdev: $(OBJS) $(TESTDIR)/test_vdev.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILDDIR)/test_driver: $(OBJS) $(TESTDIR)/test_driver.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILDDIR)/vdev_sim: $(OBJS) $(SRCDIR)/main.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

test: $(BUILDDIR)/test_protocol $(BUILDDIR)/test_streaming $(BUILDDIR)/test_vdev $(BUILDDIR)/test_driver
	./$(BUILDDIR)/test_protocol
	./$(BUILDDIR)/test_streaming
	./$(BUILDDIR)/test_vdev
	./$(BUILDDIR)/test_driver

sim: $(BUILDDIR)/vdev_sim
	./$(BUILDDIR)/vdev_sim

clean:
	rm -rf $(BUILDDIR)

compile_commands.json:
	@printf '[\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/crc16.c -o build/crc16.o",\n    "file": "src/crc16.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/protocol.c -o build/protocol.o",\n    "file": "src/protocol.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/parser.c -o build/parser.o",\n    "file": "src/parser.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/vdev.c -o build/vdev.o",\n    "file": "src/vdev.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/driver.c -o build/driver.o",\n    "file": "src/driver.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c src/main.c -o build/main.o",\n    "file": "src/main.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c tests/test_protocol.c -o build/test_protocol.o",\n    "file": "tests/test_protocol.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c tests/test_streaming.c -o build/test_streaming.o",\n    "file": "tests/test_streaming.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c tests/test_vdev.c -o build/test_vdev.o",\n    "file": "tests/test_vdev.c"\n  },\n  {\n    "directory": "%s",\n    "command": "gcc $(CFLAGS) -c tests/test_driver.c -o build/test_driver.o",\n    "file": "tests/test_driver.c"\n  }\n]\n' "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" "$$(pwd)" > compile_commands.json

.PHONY: all test sim clean compile_commands.json
