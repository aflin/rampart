CC=cc
OSNAME := $(shell uname)

CFLAGS=-Wall -g -O2 -std=c99 -I/usr/local/rampart/include 

ifeq ($(OSNAME), Linux)
	CFLAGS += -fPIC -shared -Wl,-soname,stringfuncs.so
endif
ifeq ($(OSNAME), Darwin)
	CFLAGS += -dynamiclib -Wl,-headerpad_max_install_names -undefined dynamic_lookup -install_name stringfuncs.so
endif

#LINUX
#cc -I/usr/local/rampart/include -fPIC -shared -Wl,-soname,stringfuncs.so -o stringfuncs.so stringfuncs.c
#MACOS
#cc -I/usr/local/rampart/include -dynamiclib -Wl,-headerpad_max_install_names -undefined dynamic_lookup -o stringfuncs.so -install_name stringfuncs.so stringfuncs.c

all: stringfuncs.so

stringfuncs.so: stringfuncs.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean

clean:
	rm -f ./*.so
