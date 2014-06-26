CFLAGS+=-std=c99 -Wall -Wextra -pedantic -lncurses

all: nbwmon

install: nbwmon
	mkdir -p ${DESTDIR}/usr/bin
	install -m 755 nbwmon ${DESTDIR}/usr/bin/

uninstall:
	rm -f ${DESTDIR}/usr/bin/nbwmon

clean:
	rm nbwmon
