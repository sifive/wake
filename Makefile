# Bootstrap build file

CXX     ?= g++
CFLAGS	?= -Wall -O2 -flto -DVERSION=$(VERSION)

CORE_CFLAGS  := $(shell pkg-config --cflags re2 sqlite3 ncurses)
CORE_LDFLAGS := $(shell pkg-config --libs   re2 sqlite3 ncurses)
FUSE_CFLAGS  := $(shell pkg-config --cflags fuse)
FUSE_LDFLAGS := $(shell pkg-config --libs   fuse)
VERSION      := $(shell git describe --tags --dirty)

all:		wake.db
	./bin/wake all default

wake.db:	bin/wake lib/wake/fuse-wake lib/wake/shim-wake
	test -f $@ || ./bin/wake --init .

install:	all
	./bin/wake install '"install"'

bin/wake:	$(patsubst %.cpp,%.o,$(wildcard src/*.cpp)) src/symbol.o
	$(CXX) -std=c++11 $(CFLAGS) -o $@ $^ $(CORE_LDFLAGS) -lgmp -lutf8proc

lib/wake/fuse-wake:	fuse/fuse.cpp
	$(CXX) -std=c++11 $(CFLAGS) $(FUSE_CFLAGS) $< -o $@ $(FUSE_LDFLAGS)

lib/wake/shim-wake:	$(patsubst %.cpp,%.o,$(wildcard shim/*.cpp))
	$(CXX) -std=c++11 $(CFLAGS) -o $@ $^

%.o:	%.cpp	$(filter-out src/version.h,$(wildcard src/*.h) $(wildcard shim/*.h))
	$(CXX) -std=c++11 $(CFLAGS) $(CORE_CFLAGS) -o $@ -c $<

%.cpp:	%.re
	re2c -8 --no-generation-date $< > $@.tmp
	mv $@.tmp $@

.PRECIOUS:	src/symbol.cpp
