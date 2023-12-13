CC = gcc
CFLAGS = -m64 -pthread

main: main.o tls.o
        $(CC) $(CFLAGS) -o main main.o tls.o -lrt

tls.o: tls.c
        $(CC) $(CFLAGS) -c -lpthread tls.c

main.o: main.c
        $(CC) $(CFLAGS) -c main.c

clean:
        $(RM) main main.o tls.o
