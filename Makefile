CFLAGS+=-std=c99 -Wall -pedantic -lncurses

all: nbwmon

install: all
	mkdir -p ${DESTDIR}/usr/bin
	install -m 755 nbwmon ${DESTDIR}/usr/bin/

uninstall:
	rm -f ${DESTDIR}/usr/bin/nbwmon

clean:
	rm nbwmon
