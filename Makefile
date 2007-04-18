CFLAGS=-Wall -Winline -g -fno-strict-aliasing -Wstrict-prototypes $(COPT)
CC=gcc-4.1

COPT=-O3 -DLIMIT=128 # For performance
#COPT=-pg -O3 -DLIMIT=128 -ftest-coverage -fprofile-arcs # For profiling
#COPT=-DPy_DEBUG=1 -DLIMIT=8    # For debug mode

LDFLAGS=-g -shared  $(COPT)
LD=$(CC)
LOADLIBES=-lpython2.5

cblist.so: cblist.o
	$(LD) $(LDFLAGS) -o $@ $< $(LOADLIBES)

clean:
	rm -f *.o *.so