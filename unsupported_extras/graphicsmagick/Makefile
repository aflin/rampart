
CC=cc
OSNAME := $(shell uname)
GFLAGS = $(shell GraphicsMagickWand-config --cppflags --ldflags --libs)

CFLAGS=-Wall -g -O2 $(GFLAGS) -std=c99 -I/usr/local/rampart/include

ifeq ($(OSNAME), Linux)
	CFLAGS += -fPIC -shared -Wl,-soname,rampart-gm.so
endif
ifeq ($(OSNAME), FreeBSD)
	CFLAGS += -fPIC -shared -Wl,-soname,rampart-gm.so
endif
ifeq ($(OSNAME), Darwin)
	CFLAGS += -dynamiclib -Wl,-headerpad_max_install_names -undefined dynamic_lookup -install_name rampart-gm.so
	CFLAGS += -L$(shell brew --prefix libtool)/lib
endif

all: rampart-gm.so

rampart-gm.so: gm.c
	$(CC) $^ $(CFLAGS) -o $@

.PHONY: clean

clean:
	rm -f ./*.so

