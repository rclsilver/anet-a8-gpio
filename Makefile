CC = gcc
CFLAGS = -std=c11 -W -Wall -g
LDFLAGS =
DESTDIR =
 
SRC = $(wildcard *.c)
OBJS = $(SRC:.c=.o)
PROG = anet-a8-gpio

.PHONY: all build install clean

all : build
 
build : $(OBJS)
	$(CC) $(LDFLAGS) -o $(PROG) $^

%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<

install : build
	mkdir -p $(DESTDIR)/usr/sbin
	mkdir -p $(DESTDIR)/etc/systemd/system
	cp $(PROG) $(DESTDIR)/usr/sbin
	cp $(PROG).service $(DESTDIR)/etc/systemd/system

uninstall :
	@rm $(DESTDIR)/usr/sbin/$(PROG) 2>/dev/null || /bin/true
	@systemctl stop $(PROG).service 2>/dev/null || /bin/true
	@systemctl disable $(PROG).service 2>/dev/null || /bin/true
	@rm $(DESTDIR)/etc/systemd/system/$(PROG).service 2>/dev/null || /bin/true
	@systemctl daemon-reload

clean :
	@rm *.o 2>/dev/null || /bin/true
	@rm $(PROG) 2>/dev/null || /bin/true
