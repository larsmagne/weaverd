GMIME_CONFIG = /usr/bin/pkg-config
GMIME_CFLAGS = `$(GMIME_CONFIG) gmime-2.6 --cflags`
GMIME_LIBS = `$(GMIME_CONFIG) gmime-2.6 --libs`

HEADER_FILES=config.h util.h hash.h weaver.h input.h dispatch.h

CPPFLAGS=$(GMIME_CFLAGS) -I/usr/local/include -g -O3 -Wall
LDFLAGS=$(GMIME_LIBS)
CC = gcc $(CPPFLAGS)

all: weaverd int simple

hash.o: hash.c $(HEADER_FILES)

weaver.o: weaver.c $(HEADER_FILES)

input.o: input.c $(HEADER_FILES)

int.o: int.c $(HEADER_FILES)

util.o: util.c $(HEADER_FILES)

dispatch.o: dispatch.c $(HEADER_FILES)

daemon.o: daemon.c $(HEADER_FILES)

weaverd: hash.o weaver.o input.o util.o daemon.o dispatch.o
	$(CC) $(CPPFLAGS) -o weaverd $(LDFLAGS) hash.o weaver.o input.o util.o daemon.o dispatch.o

int: hash.o weaver.o input.o util.o daemon.o int.o
	$(CC) $(CPPFLAGS) -o int $(LDFLAGS) hash.o weaver.o input.o util.o int.o

simple: hash.o weaver.o input.o util.o daemon.o int.o simple.o
	$(CC) $(CPPFLAGS) -o simple $(LDFLAGS) hash.o weaver.o input.o util.o simple.o

clean:
	$(RM) indexer *.o

TAGS: *.h *.c
	etags *.[ch]
