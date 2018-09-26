# Bootstrap build file

GMP     ?= /opt/local
GMP_INC ?= $(GMP)/include
GMP_LIB ?= $(GMP)/lib
CFLAGS	?= -Wall -Wextra -Wno-unused-parameter -O2 -flto

all:	wake fuse-wake
	./wake --init .
	./wake -v all default

wake:	$(patsubst %.cpp,%.o,$(wildcard src/*.cpp)) src/symbol.o
	g++ -std=c++11 $(CFLAGS) -L $(GMP_LIB) -o $@ $^ -lgmp -lre2 -lsqlite3

fuse-wake:	fuse/fuse.cpp
	g++ -std=c++11 $(CFLAGS) `pkg-config fuse --cflags --libs` $< -o $@

%.o:	%.cpp	$(wildcard src/*.h)
	g++ -std=c++11 $(CFLAGS) -I $(GMP_INC) -o $@ -c $<

%.cpp:	%.re
	re2c $< > $@.tmp
	mv $@.tmp $@
