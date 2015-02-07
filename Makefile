# On NetBSD you need to install ncurses and
# add -I/usr/pkg/include and -L/usr/pkg/lib to the options
# below.
CFLAGS += -std=c99 -pedantic -Wall -Wextra
LDLIBS = -lncurses
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/man

BIN = nbwmon
OBJ = nbwmon.o util.o

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): util.h

clean:
	rm -f $(BIN) $(OBJ)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin/
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f $(BIN).1 $(DESTDIR)$(MANPREFIX)/man1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1

.PHONY:
	all clean install uninstall
