wideml:	$(patsubst %.cpp,%.o,$(wildcard *.cpp)) symbol.o
	g++ -o $@ $^ -L/opt/local/lib -lgmp -losxfuse

%.o:	%.cpp	$(wildcard *.h)
	g++ -Wall -O2 -o $@ -c $<

%.cpp:	%.re
	~/re2c-1.0.1/re2c $< > $@.tmp
	mv $@.tmp $@
