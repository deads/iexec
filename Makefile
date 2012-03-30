include make.inc

CFLAGS += -D_GNU_SOURCE -ggdb

all: src/iexec

src/iexec: src/iexec.o 

src/iexec.o: src/iexec.c src/iexec-help.h src/iexec-help-nontty.h src/config.h

# A rule for making an html file.
html/%.html: html/%.pod
	pod2html --podroot html/ --cachedir html/ --title "$*" --infile "html/$*.pod" --outfile "html/$*.html"

# A rule for converting the manpage into a text file, putting this text in
# a C variable to be used by a C program's usage() function, and removing
# the txt.
#
# The -nontty rule does the same but strips ANSI escape sequences.
src/%-help.h: src/%.pod
	# This is a bit messy but it avoids using recursive makefiles, which
	# have known issues.
	pod2text -c $< src/$*.txt
	cd src && xxd -i $*.txt $*-help.h
	rm src/$*.txt

src/%-help-nontty.h: src/%.pod
	# Ditto.
	pod2text $< src/$*-nontty.txt
	cd src && xxd -i $*-nontty.txt $*-help-nontty.h
	rm src/$*-nontty.txt

install: src/iexec
	mkdir -p $(install_dir)/bin
	mkdir -p $(install_dir)/share/man/man1
	install -T src/iexec $(install_dir)/bin/iexec

clean:
	rm src/iexec src/iexec.o