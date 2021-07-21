CC=cc

CFLAGS=-Wall -g -O2 -std=c99 -I/usr/local/rampart/include -fPIC -shared

#cc -I/usr/local/rampart/include -fPIC -shared -Wl,-soname,stringfuncs.so -o stringfuncs.so stringfuncs.c

all: stringfuncs.so

stringfuncs.so: stringfuncs.c
	$(CC) $(CFLAGS) -Wl,-soname,$@ -o $@ $^

.PHONY: clean

clean:
	rm -f ./*.so
    