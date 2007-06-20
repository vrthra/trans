LIBS = -lsocket -lnsl

all:
	g++ trance.cpp -o trance $(LIBS)
