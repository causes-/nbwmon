CFLAGS=-std=c99 -Wall -Wextra -pedantic -g -lncurses

all: nbwmon

install: nbwmon
	mkdir -p ${DESTDIR}/bin
	install -m 755 nbwmon ${DESTDIR}/bin/

uninstall:
	rm -f ${DESTDIR}/bin/nbwmon

clean:
	rm nbwmon
