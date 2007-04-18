CFLAGS=-Wall -Winline -g -fno-strict-aliasing -Wstrict-prototypes $(COPT)
CC=gcc-4.1

COPT=-O3 -DLIMIT=128 # For performance
#COPT=-pg -O3 -DLIMIT=128 -ftest-coverage -fprofile-arcs # For profiling
#COPT=-DPy_DEBUG=1 -DLIMIT=8    # For debug mode

LDFLAGS=-g -shared  $(COPT)
LD=$(CC)
LOADLIBES=-lpython2.5

blist.so: blist.o
	$(LD) $(LDFLAGS) -o $@ $< $(LOADLIBES)

clean:
	rm -f *.o *.so

tarball:
	cd .. ; tar -zcvf blist.tar.gz blist/blist.c blist/Makefile blist/test_blist.py blist/test/*.py

