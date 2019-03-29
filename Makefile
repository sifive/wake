# Bootstrap build file

VERSION	:= $(shell git describe --tags --dirty)

CC	:= gcc -std=c99
CXX	:= g++ -std=c++11
CFLAGS	:= -Wall -O2 -flto -DVERSION=$(VERSION)

CORE_CFLAGS  := $(shell pkg-config --cflags sqlite3 ncurses)
CORE_LDFLAGS := $(shell pkg-config --libs   sqlite3 ncurses)
FUSE_CFLAGS  := $(shell pkg-config --cflags fuse)
FUSE_LDFLAGS := $(shell pkg-config --libs   fuse)

all:		wake.db
	./bin/wake all default

wake.db:	bin/wake lib/wake/fuse-wake lib/wake/shim-wake
	test -f $@ || ./bin/wake --init .

install:	all
	./bin/wake install '"install"'

bin/wake:	$(patsubst %.cpp,%.o,$(wildcard src/*.cpp)) src/symbol.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CORE_LDFLAGS) -lgmp -lutf8proc -lre2

lib/wake/fuse-wake:	fuse/fuse.cpp
	$(CXX) $(CFLAGS) $(FUSE_CFLAGS) $< -o $@ $(FUSE_LDFLAGS)

lib/wake/shim-wake:	$(patsubst %.c,%.o,$(wildcard shim/*.c))
	$(CC) $(CFLAGS) -o $@ $^

%.o:	%.cpp	$(filter-out src/version.h,$(wildcard */*.h))
	$(CXX) $(CFLAGS) $(CORE_CFLAGS) -o $@ -c $<

%.o:	%.c	$(filter-out src/version.h,$(wildcard */*.h))
	$(CC) $(CFLAGS) $(CORE_CFLAGS) -o $@ -c $<

# Rely on wake to recreate this file if re2c is available
%.cpp:	%.cpp.gz
	gzip -dc $^ > $@.tmp
	mv $@.tmp $@

.PRECIOUS:	src/symbol.cpp
