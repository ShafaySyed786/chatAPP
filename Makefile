CC = gcc

CFLAGS = -g -pedantic -Wall

all: mytalk

mytalk: mytalk.o
	$(CC) $(CFLAGS) -L ~pn-cs357/Given/Talk/lib64 -o mytalk mytalk.o -ltalk -lncurses

mytalk.o: mytalk.c
	$(CC) $(CFLAGS) -c -I ~pn-cs357/Given/Talk/include mytalk.c

clean: mytalk
	rm -f *.o