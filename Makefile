SOLARIS_LIBS = -lsocket -lnsl


all:
	g++ trance.cpp -o trance

solaris:
	g++ trance.cpp -o trance $(SOLARIS_LIBS)
