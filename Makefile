# Bootstrap build file

CXX     ?= g++
GMP     ?= /opt/local
GMP_INC ?= $(GMP)/include
GMP_LIB ?= $(GMP)/lib
CFLAGS	?= -Wall -O2 -flto

all:		wake.db
	./bin/wake all default

wake.db:	bin/wake lib/wake/fuse-wake
	test -f $@ || ./bin/wake --init .

install:	all
	./bin/wake install '"install"'

bin/wake:	$(patsubst %.cpp,%.o,$(wildcard src/*.cpp)) src/symbol.o
	$(CXX) -std=c++11 $(CFLAGS) -L $(GMP_LIB) -o $@ $^ -lgmp -lre2 -lsqlite3 -lutf8proc

lib/wake/fuse-wake:	fuse/fuse.cpp
	$(CXX) -std=c++11 $(CFLAGS) `pkg-config --cflags fuse` $< -o $@ `pkg-config --libs fuse`

%.o:	%.cpp	$(wildcard src/*.h)
	$(CXX) -std=c++11 $(CFLAGS) -I $(GMP_INC) -o $@ -c $<

%.cpp:	%.re
	re2c -8 --no-generation-date $< > $@.tmp
	mv $@.tmp $@

.PRECIOUS:	src/symbol.cpp
