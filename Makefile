wideml:	$(patsubst %.cpp,%.o,$(wildcard *.cpp))
	g++ -o $@ $^ -L/opt/local/lib -lgmp -losxfuse

%.o:	%.cpp	$(wildcard *.h)
	g++ -Wall -O2 -o $@ -c $<
