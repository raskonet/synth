CC     = gcc
CFLAGS = -O2 -Wall -Wno-unused-function -Ilayer1/include -Ilayer0/include
L0SRC  = layer0/src/patch_interp.c layer0/src/tables.c
L1SRC  = layer1/src/voice.c

all: test_layer1

test_layer1: layer1/tests/test_layer1.c $(L1SRC) $(L0SRC)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -f test_layer1
.PHONY: all clean
