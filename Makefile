# On NetBSD you need to install ncurses and
# add -I/usr/pkg/include and -L/usr/pkg/lib to the options
# below.
CFLAGS+=-std=c99 -pedantic -Wall -Wextra
LDLIBS=-lncurses
PREFIX=/usr/local

BIN=nbwmon
OBJ=nbwmon.o util.o

$(BIN): $(OBJ)

all: $(BIN)

clean:
	rm -f $(BIN) $(OBJ)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
