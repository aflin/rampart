#
#  Example Makefile for building a program with embedded Duktape.
#  The example program here is the Duktape command line tool.
#

DUKTAPE_SOURCES = duktape.c

CMDLINE_SOURCES = duk_cmdline.c

CC = gcc
CCOPTS = -Os -pedantic -std=c99 -Wall -fstrict-aliasing -fomit-frame-pointer
CCOPTS += -I./examples/cmdline -I./src   # duktape.h and duk_config.h must be in include path
CCLIBS = -lm

# Enable print() and alert() for command line using an optional extra module.
CCOPTS += -DDUK_CMDLINE_PRINTALERT_SUPPORT
CMDLINE_SOURCES += duk_print_alert.c

# Enable console object (console.log() etc) for command line.
CCOPTS += -DDUK_CMDLINE_CONSOLE_SUPPORT
CMDLINE_SOURCES += duk_console.c

# Enable Duktape.Logger for command line.
CCOPTS += -DDUK_CMDLINE_LOGGING_SUPPORT
CMDLINE_SOURCES += duk_logging.c

# Enable Duktape 1.x module loading for command line.
CCOPTS += -DDUK_CMDLINE_MODULE_SUPPORT
CMDLINE_SOURCES += duk_module_duktape.c

# Enable fileio support
CCOPTS += -DDUK_CMDLINE_FILEIO

# DB test
CCOPTS += -DDUK_DB_SUPPORT
CMDLINE_SOURCES += db.c

# If you want linenoise, you can enable these.  At the moment linenoise
# will cause some harmless compilation warnings.
#CCOPTS += -DDUK_CMDLINE_FANCY -I./linenoise
#CMDLINE_SOURCES += linenoise/linenoise.c
#duk: linenoise

# Use the tools/configure.py utility to modify Duktape default configuration:
# http://duktape.org/guide.html#compiling
# http://wiki.duktape.org/Configuring.html

duk: $(DUKTAPE_SOURCES) $(CMDLINE_SOURCES)
	$(CC) -o $@ $(DEFINES) $(CCOPTS) $(DUKTAPE_SOURCES) $(CMDLINE_SOURCES) $(CCLIBS)

linenoise/linenoise.c: linenoise
linenoise:
	git clone https://github.com/antirez/linenoise.git
