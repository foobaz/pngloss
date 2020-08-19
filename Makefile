-include config.mk

CC ?= /usr/bin/cc
CFLAGS = -O3 -Wall -Wextra -I/usr/local/include
LDFLAGS = -L/usr/local/lib -lpng
VERSION = 1.0

BIN ?= pngloss
DESTDIR ?= /usr/local
BINPREFIX ?= $(DESTDIR)$(PREFIX)/bin
MANPREFIX ?= $(DESTDIR)$(PREFIX)/share/man

OBJS = src/color_delta.o src/optimize_state.o src/pngloss_image.o src/pngloss_opts.o src/pngloss.o src/rwpng.o

DISTFILES = pngloss.1 Makefile README.md COPYRIGHT
TARNAME = pngloss-$(VERSION)
TARFILE = $(TARNAME)-src.tar.gz

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(CFLAGS) $(LDFLAGS) -o $@

dist: $(TARFILE)

$(TARFILE): $(DISTFILES)
	test -n "$(VERSION)"
	rm -rf $(TARFILE) $(TARNAME)
	mkdir $(TARNAME)
	cp $(DISTFILES) $(TARNAME)
	tar -czf $(TARFILE) --numeric-owner --exclude='._*' $(TARNAME)
	rm -rf $(TARNAME)
	-shasum $(TARFILE)

install: $(BIN) $(BIN).1
	-mkdir -p '$(BINPREFIX)'
	-mkdir -p '$(MANPREFIX)/man1'
	install -m 0755 -p '$(BIN)' '$(BINPREFIX)/$(BIN)'
	install -m 0644 -p '$(BIN).1' '$(MANPREFIX)/man1/'

uninstall:
	rm -f '$(BINPREFIX)/$(BIN)'
	rm -f '$(MANPREFIX)/man1/$(BIN).1'

clean:
	rm -f '$(BIN)' $(OBJS) $(TARFILE)

distclean: clean
	rm -f pngquant-*-src.tar.gz
