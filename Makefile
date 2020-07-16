-include config.mk

CC ?= /usr/bin/cc
CFLAGS = -O3 -mcpu=power9 -mtune=power9 -Wall -Wextra -I/usr/local/include
#CFLAGS = -O3 -Wall -Wextra -I/usr/local/include
LDFLAGS = -L/usr/local/lib -lpng
VERSION = 0.4

BIN ?= pngloss
BINPREFIX ?= $(DESTDIR)$(PREFIX)/bin
MANPREFIX ?= $(DESTDIR)$(PREFIX)/share/man

OBJS = color_delta.o optimize_state.o pngloss_filters.o pngloss_image.o pngloss_opts.o pngloss.o rwpng.o

DISTFILES = Makefile README.md COPYRIGHT
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

uninstall:
	rm -f '$(BINPREFIX)/$(BIN)'

clean:
	rm -f '$(BIN)' $(OBJS) $(TARFILE)

distclean: clean
	rm -f pngquant-*-src.tar.gz
