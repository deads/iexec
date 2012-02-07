include make.inc

all: src/iexec

src/iexec: src/iexec.o 

src/iexec.o: src/iexec.c

install: src/iexec
	install -T src/iexec $(PREFIX)/bin

clean:
	rm src/iexec src/iexec.o