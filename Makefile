CC     = cc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2 -D_DEFAULT_SOURCE

# make DEBUG=1: debug symbols + sanitizers (crash loudly on memory bugs)
ifdef DEBUG
CFLAGS += -O0 -g -fsanitize=address,undefined
endif

SRC = $(wildcard src/*.c)            # every .c file in src/
OBJ = $(SRC:src/%.c=build/%.o)       # src/foo.c -> build/foo.o

magisc: $(OBJ)                      # link: all .o files -> one program
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

clean:
	rm -rf build magisc

-include $(OBJ:.o=.d)                # rebuild .o files when a header changes
.PHONY: clean test
