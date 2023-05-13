CC=gcc
CFLAGS=-O3 -Wall
LDFLAGS=
#LDFLAGS+=-D_GNU_SUORCE
LDFLAGS+=-L/lib/x86_64-linux-gnu -levent_core -levent_extra -levent_pthreads
SOURCES=med_serv.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=med_serv
DIR=$(HOME)/.bin-libevent

all:
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $(EXECUTABLE)
	
#$(EXECUTABLE): $(OBJECTS) 
#	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(OBJECTS) -o $@

#.c.o:
#	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

#gcc -c -O3 -Wall med_serv.c -D_GNU_SUORCE -L/lib/x86_64-linux-gnu -levent_core -levent