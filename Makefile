# Bootstrap build file

VERSION	:= $(shell if test -f manifest.wake; then sed -n "/publish releaseAs/ s/^[^']*'\([^']*\)'.*/\1/p" manifest.wake; else git describe --tags --dirty; fi)

CC	:= cc -std=c11
CXX	:= c++ -std=c++11
CFLAGS	:= -Wall -O2 -DVERSION=$(VERSION)
LDFLAGS	:=
DESTDIR ?= /usr/local

LOCAL_CFLAGS :=	-Iutf8proc -Igopt -Icommon -Isrc
FUSE_CFLAGS  :=	$(shell pkg-config --silence-errors --cflags fuse)
CORE_CFLAGS  := $(shell pkg-config --silence-errors --cflags sqlite3)	\
		$(shell pkg-config --silence-errors --cflags gmp-6)	\
		$(shell pkg-config --silence-errors --cflags re2)	\
		$(shell pkg-config --silence-errors --cflags-only-I ncurses)
FUSE_LDFLAGS := $(shell pkg-config --silence-errors --libs fuse    || echo -lfuse)
CORE_LDFLAGS :=	$(shell pkg-config --silence-errors --libs sqlite3 || echo -lsqlite3)	\
		$(shell pkg-config --silence-errors --libs gmp-6   || echo -lgmp)	\
		$(shell pkg-config --silence-errors --libs re2     || echo -lre2)	\
		$(shell pkg-config --silence-errors --libs ncurses tinfo || pkg-config --silence-errors --libs ncurses || echo -lncurses)

COMMON := common/jlexer.o $(patsubst %.cpp,%.o,$(wildcard common/*.cpp)) $(patsubst %.c,%.o,$(wildcard common/*.c))
WAKE_ENV := WAKE_PATH=$(shell dirname $(shell which $(firstword $(CC))))

# If FUSE is unavalable during wake build, allow a linux-specific work-around
ifeq ($(USE_FUSE_WAKE),0)
EXTRA := lib/wake/libpreload-wake.so bin/preload-wake
endif

all:		wake.db
	$(WAKE_ENV) ./bin/wake build default

clean:
	rm -f bin/* lib/wake/* */*.o common/jlexer.cpp src/frontend/lexer.cpp src/frontend/parser.cpp src/version.h wake.db
	touch bin/stamp lib/wake/stamp

wake.db:	bin/wake bin/fuse-wake lib/wake/fuse-waked lib/wake/shim-wake $(EXTRA)
	test -f $@ || ./bin/wake --init .

install:	all
	$(WAKE_ENV) ./bin/wake install $(DESTDIR)

test:		wake.db
	$(WAKE_ENV) ./bin/wake --in test_wake runTests

tarball:	wake.db
	$(WAKE_ENV) ./bin/wake build tarball

vscode:		wake.db
	$(WAKE_ENV) ./bin/wake vscode

static:	wake.db
	$(WAKE_ENV) ./bin/wake static

bin/wake:	src/frontend/lexer.o src/frontend/parser.o $(COMMON)	\
		$(patsubst %.cpp,%.o,$(wildcard src/*/*.cpp))		\
		$(patsubst %.c,%.o,utf8proc/utf8proc.c gopt/gopt.c gopt/gopt-errors.c gopt/gopt-arg.c)
	$(CXX) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(CORE_LDFLAGS)

bin/fuse-wake:	fuse/client.cpp fuse/fuse.cpp fuse/namespace.cpp fuse/daemon_client.cpp $(COMMON)
	$(CXX) $(CFLAGS) $(LOCAL_CFLAGS) $^ -o $@ $(LDFLAGS)

lib/wake/fuse-waked:	fuse/daemon.cpp $(COMMON)
	$(CXX) $(CFLAGS) $(LOCAL_CFLAGS) $(FUSE_CFLAGS) $^ -o $@ $(LDFLAGS) $(FUSE_LDFLAGS)

lib/wake/shim-wake:	$(patsubst %.c,%.o,$(wildcard shim/*.c))
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bin/preload-wake:	preload/wrap.cpp $(COMMON)
	$(CXX) $(CFLAGS) $(LOCAL_CFLAGS) -DEXT=so -DENV=LD_PRELOAD -o $@ $^ $(LDFLAGS)

lib/wake/libpreload-wake.so:	preload/open.c
	$(CC) $(CFLAGS) -fpic -shared -o $@ $^ $(LFDLAGS) -ldl

%.o:	%.cpp	$(filter-out src/version.h,$(wildcard */*.h) $(wildcard */*/*.h) src/frontend/parser.h)
	$(CXX) $(CFLAGS) $(LOCAL_CFLAGS) $(CORE_CFLAGS) -o $@ -c $<

%.o:	%.c	$(filter-out src/version.h,$(wildcard */*.h))
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) -o $@ -c $<

# Rely on wake to recreate this file if re2c is available
%.cpp:	%.cpp.gz
	gzip -dc $^ > $@.tmp
	mv -f $@.tmp $@

%.h:	%.h.gz
	gzip -dc $^ > $@.tmp
	mv -f $@.tmp $@

.PRECIOUS:	src/frontend/lexer.cpp src/frontend/parser.cpp src/frontend/parser.h common/jlexer.cpp
.SUFFIXES:
