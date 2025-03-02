CC = gcc
CFLAGS = -g -Wall -luring -lpthread

.PHONY: all clean
all: main

main: main.o
	${CC} $^ -o $@ ${CFLAGS}

main.o: main.c
	${CC} -c $<

clean:
	rm -rf main
	rm ./*.o
