.PHONY: clean

LDLIBS=../../obj/libst.a
CFLAGS=-g -O0

./verify:  verify.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wall -o $@ $^ $(LDLIBS)

clean:
	rm -f verify

