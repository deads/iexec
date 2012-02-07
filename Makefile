include make.inc

all: src/iexec

src/iexec: src/iexec.o 

src/iexec.o: src/iexec.c

install: src/iexec
	mkdir -p $(install_dir)/bin
	mkdir -p $(install_dir)/share/man/man1
	install -T src/iexec $(install_dir)/bin/iexec

clean:
	rm src/iexec src/iexec.o