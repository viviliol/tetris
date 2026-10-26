CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
LDLIBS   = $(shell pkg-config --libs glfw3) -lGL
CPPFLAGS = $(shell pkg-config --cflags glfw3)

tetris: tetris.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LDLIBS)

original/tetris: original/tetris.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LDLIBS)

# Замеры «до/после» (откроют окна игры на время прогона)
bench:
	./profiling/run_all.sh

bench-full:
	./profiling/run_all.sh --callgrind

.PHONY: bench bench-full
