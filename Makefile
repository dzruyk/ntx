CC = gcc
LD = ld
CFLAGS = -Wall -O0 -g -D_XOPEN_SOURCE=600 -DG_ENABLE_DEBUG -D_CLIENT_DEBUG `pkg-config --cflags gtk+-x11-2.0`
#CFLAGS = -Wall -O2 -D_XOPEN_SOURCE=600 `pkg-config --cflags gtk+-x11-2.0`
CFLAGS += -I/usr/include/fontconfig
LIBS = `pkg-config --libs gtk+-x11-2.0` -lfreetype -lfontconfig
OBJECTS = fc.o fontsel.o console.o nvt.o client.o gui.o key.o chn.o \
	  chn_telnet.o chn_echo.o chn_pty.o fiorw.o
HEADERS = internal.h nvt.h console.h
BINARIES = ntx test_console test_fio test_spawn fio

COMPILE = $(CC) $(CFLAGS) $(LIBS)

all: $(BINARIES)

fc.o: fc.c fc.h
	$(COMPILE) -c -o $@ $<

fontsel.o: fontsel.c fontsel.h
	$(COMPILE) -c -o $@ $<

console.o: console.c console.h
	$(COMPILE) -c -o $@ $<

nvt.o: nvt.c nvt.h
	$(COMPILE) -c -o $@ $<

key.o: key.c internal.h
	$(COMPILE) -c -o $@ $<

gui.o: gui.c internal.h
	$(COMPILE) -c -o $@ $<

chn.o: chn.c chn.h
	$(COMPILE) -c -o $@ $<

chn_echo.o: chn_echo.c chn.h
	$(COMPILE) -c -o $@ $<

chn_telnet.o: chn_telnet.c chn.h nvt.h
	$(COMPILE) -c -o $@ $<

chn_pty.o: chn_pty.c chn.h
	$(COMPILE) -c -o $@ $<

client.o: client.c internal.h
	$(COMPILE) -c -o $@ $<

fiorw.o: fiorw.c fiorw.h
	$(COMPILE) -c -o $@ $<

ntx: main.c $(OBJECTS) $(HEADERS)
	$(COMPILE) -o $@ main.c $(OBJECTS)

test_console: test_console.c console.o fontsel.o fc.o
	$(COMPILE) -o $@ $^

test_fio: CFLAGS += -D_GNU_SOURCE
test_fio: test_fio.c fiorw.o
	$(COMPILE) -o $@ $^

test_spawn: test_spawn.c
	$(COMPILE) -o $@ $^

fio: fio.o
	$(COMPILE) -o $@ $<

clean:
	-rm -rf *.o $(BINARIES) *~

