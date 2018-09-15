GMP     ?= /opt/local
GMP_INC ?= $(GMP)/include
GMP_LIB ?= $(GMP)/lib
CFLAGS	?= -Wall -Wextra -Wno-unused-parameter -O2 -flto

wake:	$(patsubst %.cpp,%.o,$(wildcard *.cpp)) symbol.o
	g++ -std=c++11 $(CFLAGS) -L $(GMP_LIB) -o $@ $^ -lgmp -lre2

%.o:	%.cpp	$(wildcard *.h)
	g++ -std=c++11 $(CFLAGS) -I $(GMP_INC) -o $@ -c $<

%.cpp:	%.re
	re2c $< > $@.tmp
	mv $@.tmp $@
