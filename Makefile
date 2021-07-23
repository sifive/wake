parser:		parser.o lexer.o syntax.o file.o location.o
	g++ -Wall -O2 -o $@ $^

%.y:		%.y.m4
	m4 $< > $@.out
	mv $@.out $@

%.h %.cpp:		%.y
	lemon $<
	mv $*.c $*.cpp

%.o:		%.cpp parser.h lexer.h
	g++ -std=c++11 -Wall -O2 -o $@ -c $<

lexer.cpp:	lexer.re
	re2c --no-generation-date --input-encoding utf8 $< > $@.tmp
	mv $@.tmp $@

.SUFFIXES:
