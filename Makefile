CFLAGS = -std=c++17 -fpermissive -march=haswell -mtune=haswell -Wall -O2

parser:		parser.o lexer.o syntax.o file.o location.o main.o cst.o main.o rank.o
	g++ $(CFLAGS) -o $@ $^

%.y:		%.y.m4
	m4 $< > $@.out
	mv $@.out $@

%.h %.cpp:		%.y
	lemon $<
	mv $*.c $*.cpp

%.o:		%.cpp parser.h lexer.h
	g++ $(CFLAGS) -o $@ -c $<

lexer.cpp:	lexer.re
	re2c --no-generation-date --input-encoding utf8 $< > $@.tmp
	mv $@.tmp $@

.SUFFIXES:
