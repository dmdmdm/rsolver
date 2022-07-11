CC = gcc
LDFLAGS = -lstdc++
CFLAGS = -g -I. -fno-rtti -fno-exceptions -Wall
CPPFLAGS = $(CFLAGS)
SOURCES = rsolver.cpp rsolver.h

all: rsolver big_test.txt

clean:
	rm -f *.o rsolver big_test.txt

format:
	clang-format -style '{BasedOnStyle: Google, DerivePointerBinding: false, Standard: Cpp11}' -i $(SOURCES)

install: all

rsolver: rsolver.o
	$(CC) $(CFLAGS) -o $@ rsolver.o $(LDFLAGS)

big_test.txt: mkbig
	./mkbig > $@

check: rsolver
	./rsolver 'x & ~b & apple'
