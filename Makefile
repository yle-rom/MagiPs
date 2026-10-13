CC     = cc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2 -D_DEFAULT_SOURCE

# make DEBUG=1: debug symbols + sanitizers (crash loudly on memory bugs)
ifdef DEBUG
CFLAGS += -O0 -g -fsanitize=address,undefined
endif

SRC = $(wildcard src/*.c)            # every .c file in src/
OBJ = $(SRC:src/%.c=build/%.o)       # src/foo.c -> build/foo.o

whatsup: $(OBJ)                      # link: all .o files -> one program
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c                   # compile: one .c -> one .o
	@mkdir -p build
	$(CC) $(CFLAGS) -MMD -c -o $@ $<

clean:
	rm -rf build whatsup

-include $(OBJ:.o=.d)                # rebuild .o files when a header changes
.PHONY: clean
