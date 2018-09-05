GMP     ?= /opt/local
GMP_INC ?= $(GMP)/include
GMP_LIB ?= $(GMP)/lib

wake:	$(patsubst %.cpp,%.o,$(wildcard *.cpp)) symbol.o
	g++ -std=c++11 -Wall -O2 -L $(GMP_LIB) -o $@ $^ -lgmp

%.o:	%.cpp	$(wildcard *.h)
	g++ -std=c++11 -Wall -O2 -I $(GMP_INC) -o $@ -c $<

%.cpp:	%.re
	~/re2c-1.0.1/re2c $< > $@.tmp
	mv $@.tmp $@
