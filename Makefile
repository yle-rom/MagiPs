CC      = cc
CFLAGS  = -std=c17 -Wall -Wextra -Wpedantic -O2 -D_DEFAULT_SOURCE
PREFIX ?= /usr/local

# make DEBUG=1: debug symbols + sanitizers (crash loudly on memory bugs)
ifdef DEBUG
CFLAGS += -O0 -g -fsanitize=address,undefined
endif

SRC = $(wildcard src/*.c)            # every .c file in src/
OBJ = $(SRC:src/%.c=build/%.o)       # src/foo.c -> build/foo.o

magips: $(OBJ)                      # link: all .o files -> one program
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c                   # compile: one .c -> one .o
	@mkdir -p build
	$(CC) $(CFLAGS) -MMD -c -o $@ $<

# tests: everything except main.o, linked with tests/*.c, run against tests/fixtures
TEST_SRC = $(wildcard tests/*.c)
LIB_OBJ  = $(filter-out build/main.o,$(OBJ))

test: $(LIB_OBJ) $(TEST_SRC) tests/test.h
	$(CC) $(CFLAGS) -Isrc -o build/run_tests $(TEST_SRC) $(LIB_OBJ)
	./build/run_tests

install: magips                      # make install PREFIX=~/.local, or sudo make install
	install -Dm755 magips $(DESTDIR)$(PREFIX)/bin/magips

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/magips

# fake machine for screenshots (nothing real is shown or killed): source build/demo/env.sh
demo: magips
	python3 demo/make_demo.py

# needs vhs (pacman -S vhs). Each tape gets a fresh demo machine.
gif: magips
	python3 demo/make_demo.py && vhs demo/demo.tape
	python3 demo/make_demo.py && vhs demo/tables.tape

clean:
	rm -rf build magips

-include $(OBJ:.o=.d)                # rebuild .o files when a header changes
.PHONY: clean test install uninstall demo gif
