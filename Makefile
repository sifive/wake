# Bootstrap build file

GMP     ?= /opt/local
GMP_INC ?= $(GMP)/include
GMP_LIB ?= $(GMP)/lib
CFLAGS	?= -Wall -O2 -flto

all:		wake.db
	./bin/wake -v all default

wake.db:	bin/wake lib/wake/fuse-wake
	test -f $@ || ./bin/wake --init .

install:	all
	./bin/wake -v install '"install"'

bin/wake:	$(patsubst %.cpp,%.o,$(wildcard src/*.cpp)) src/symbol.o
	g++ -std=c++11 $(CFLAGS) -L $(GMP_LIB) -o $@ $^ -lgmp -lre2 -lsqlite3

lib/wake/fuse-wake:	fuse/fuse.cpp
	g++ -std=c++11 $(CFLAGS) `pkg-config --cflags fuse` $< -o $@ `pkg-config --libs fuse`

%.o:	%.cpp	$(wildcard src/*.h)
	g++ -std=c++11 $(CFLAGS) -I $(GMP_INC) -o $@ -c $<

%.cpp:	%.re
	re2c $< > $@.tmp
	mv $@.tmp $@

.PRECIOUS:	src/symbol.cpp
