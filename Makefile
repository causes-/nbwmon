CFLAGS+=-std=c99 -pedantic -Wall -lncurses

all: nbwmon

install: all
	mkdir -p ${DESTDIR}/usr/bin
	install -m 755 nbwmon ${DESTDIR}/usr/bin/

uninstall:
	rm -f ${DESTDIR}/usr/bin/nbwmon

clean:
	rm -f nbwmon
