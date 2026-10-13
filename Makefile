CC     = cc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2 -D_DEFAULT_SOURCE

SRC = $(wildcard src/*.c)            # every .c file in src/
OBJ = $(SRC:src/%.c=build/%.o)       # src/foo.c -> build/foo.o

whatsup: $(OBJ)                      # link: all .o files -> one program
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c                   # compile: one .c -> one .o
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build whatsup

.PHONY: clean
