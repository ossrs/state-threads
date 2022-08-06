.PHONY: default clean pre

CFLAGS=-g -O0

default: ./jmpbuf pre

./jmpbuf:  jmpbuf.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -o $@ $^ $(LDLIBS)

pre:  jmpbuf.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -E -o jmpbuf.E.c $^ $(LDLIBS)

clean:
	rm -f jmpbuf jmpbuf.E.c

