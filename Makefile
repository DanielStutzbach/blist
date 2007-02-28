CFLAGS=-Wall -g -fno-strict-aliasing -Wstrict-prototypes -I/home/agthorr/src-other/Python-2.5/Include -I/home/agthorr/src-other/Python-2.5
LDFLAGS=-g -shared -Wl,--enable-auto-image-base -L/home/agthorr/src-other/Python-2.5/
LDFLAGS=-g -L/home/agthorr/src-other/Python-2.5/
LD=gcc
LOADLIBES=-lpython2.5

#cblist.dll: cblist.o
#	$(LD) $(LDFLAGS) -o $@ $< $(LOADLIBES)

cblist: cblist.o main.o

clean:
	rm -f *.o *.dll