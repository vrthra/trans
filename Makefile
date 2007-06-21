SOLARIS_LIBS = -lsocket -lnsl
OUT=trance
SRC=trance.cpp

all:
	g++ $(SRC) -o $(OUT)

solaris:
	g++ $(SRC) -o $(OUT) $(SOLARIS_LIBS)
