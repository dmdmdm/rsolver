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
	perl mkbig > $@

check_simple: rsolver
	time ./rsolver '~(a & ~b)'

check_medium: rsolver medium_test.txt
	time ./rsolver < medium_test.txt

check_big: rsolver big_test.txt
	time ./rsolver < big_test.txt

check: rsolver
	time ./rsolver 'a & ~b & c & d & e & f & g & h & i & j & k & l & m & n & o & p & q & r & s & t & u & v & w'
