# Bootstrap build file

VERSION	:= $(shell git describe --tags --dirty)

CC	:= gcc -std=c99
CXX	:= g++ -std=c++11
CFLAGS	:= -Wall -O2 -flto -DVERSION=$(VERSION)
LDFLAGS	:=

LOCAL_CFLAGS :=	-Iutf8proc -Igopt -Icommon
FUSE_CFLAGS  :=	$(shell pkg-config --silence-errors --cflags fuse)
CORE_CFLAGS  := $(shell pkg-config --silence-errors --cflags sqlite3)	\
		$(shell pkg-config --silence-errors --cflags gmp-6)	\
		$(shell pkg-config --silence-errors --cflags re2)	\
		$(shell pkg-config --silence-errors --cflags ncurses)
FUSE_LDFLAGS := $(shell pkg-config --silence-errors --libs fuse    || echo -lfuse)
CORE_LDFLAGS :=	$(shell pkg-config --silence-errors --libs sqlite3 || echo -lsqlite3)	\
		$(shell pkg-config --silence-errors --libs gmp-6   || echo -lgmp)	\
		$(shell pkg-config --silence-errors --libs re2     || echo -lre2)	\
		$(shell pkg-config --silence-errors --libs ncurses || echo -lncurses)

all:		wake.db
	./bin/wake all default

wake.db:	bin/wake lib/wake/fuse-wake lib/wake/shim-wake
	test -f $@ || ./bin/wake --init .

install:	all
	./bin/wake install '"install"'

bin/wake:	src/symbol.o common/jlexer.o common/lexint.o				\
		$(patsubst %.cpp,%.o,$(wildcard src/*.cpp) $(wildcard common/*.cpp))	\
		$(patsubst %.c,%.o,utf8proc/utf8proc.c gopt/gopt.c gopt/gopt-errors.c)
	$(CXX) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(CORE_LDFLAGS)

lib/wake/fuse-wake:	fuse/fuse.cpp
	$(CXX) $(CFLAGS) $(FUSE_CFLAGS) $< -o $@ $(LDFLAGS) $(FUSE_LDFLAGS)

lib/wake/shim-wake:	$(patsubst %.c,%.o,$(wildcard shim/*.c))
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o:	%.cpp	$(filter-out src/version.h,$(wildcard */*.h))
	$(CXX) $(CFLAGS) $(LOCAL_CFLAGS) $(CORE_CFLAGS) -o $@ -c $<

%.o:	%.c	$(filter-out src/version.h,$(wildcard */*.h))
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) -o $@ -c $<

# Rely on wake to recreate this file if re2c is available
%.cpp:	%.cpp.gz
	gzip -dc $^ > $@.tmp
	mv -f $@.tmp $@

.PRECIOUS:	src/symbol.cpp common/jlexer.cpp
