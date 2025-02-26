CC = gcc
CFLAGS = -I./f-stack/include -I./leveldb/include
LDFLAGS = -L./f-stack/lib -L./leveldb/build -lff_api -lleveldb -lpthread

all: main

main: main.o fstack_leveldb.o
	$(CC) main.o fstack_leveldb.o -o main $(LDFLAGS)

main.o: main.c fstack_leveldb.h
	$(CC) -c main.c -o main.o $(CFLAGS)

fstack_leveldb.o: fstack_leveldb.c fstack_leveldb.h
	$(CC) -c fstack_leveldb.c -o fstack_leveldb.o $(CFLAGS)

clean:
	rm -f main main.o fstack_leveldb.o

