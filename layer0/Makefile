CC     = gcc
CFLAGS = -O2 -Wall -Wno-unused-function -Iinclude
SRCS   = src/patch_interp.c src/tables.c

all: test_layer0

test_layer0: tests/test_layer0.c $(SRCS)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -f test_layer0
.PHONY: all clean
