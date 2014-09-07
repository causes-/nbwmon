ifndef PKG_CONFIG
PKG_CONFIG=pkg-config
endif

CFLAGS+=-std=c99 -pedantic -Wall -Wextra
LDLIBS=$(shell $(PKG_CONFIG) --libs ncurses)

BIN=nbwmon

all: $(BIN)

install: all
	mkdir -p $(DESTDIR)/usr/bin
	install -m 755 $(BIN) $(DESTDIR)/usr/bin/

uninstall:
	rm -f $(DESTDIR)/usr/bin/$(BIN)

clean:
	rm -f $(BIN)
