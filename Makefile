parser:		parser.o
	gcc -Wall -O2 -o $@ $^

parser.y:	parser.y.m4
	m4 $< > $@.out
	mv $@.out $@

parser.c:	parser.y
	lemon $<

parser.o:	parser.c
	gcc -Wall -O2 -o $@ $<
