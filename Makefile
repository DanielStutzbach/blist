CFLAGS=-Wall -Winline -g -fno-strict-aliasing -Wstrict-prototypes $(COPT)
CC=gcc-4.1

COPT=-O3 -DLIMIT=128 -DNDEBUG=1 # For performance
#COPT=-pg -O3 -DLIMIT=128 -DNDEBUG=1 -ftest-coverage -fprofile-arcs # For profiling
#COPT=-DPy_DEBUG=1 -DLIMIT=8    # For debug mode

LDFLAGS=-g -shared  $(COPT)
LD=$(CC)
LOADLIBES=-lpython2.5

blist.so: blist.o
	$(LD) $(LDFLAGS) -o $@ $< $(LOADLIBES)

clean:
	rm -f *.o *.so *.dll

tarball:
	cd .. ; tar -zcvf blist.tar.gz blist/blist.c blist/Makefile blist/test_blist.py blist/test/*.py blist/prototype/blist.py

egg:
	python2.5 setup.py register
	CFLAGS='-O3 -fno-strict-aliasing' python2.5 setup.py build -f
	rm -f dist/*.asc
	python2.5 setup.py sdist bdist_egg upload -s
	rsync -e ssh dist/* webadmin@stutzbachenterprises.com:stutzbachenterprises/html/blist/

html:
	rst2html README.txt

speed:
	python2.5 speed_test.py 
	rsync -e ssh -r fig/* webadmin@stutzbachenterprises.com:stutzbachenterprises/html/fig/

test: COPT=-DLIMIT=8 -DPy_DEBUG=1 -I/home/agthorr/src-other/Python-2.5.1/Include -I/home/agthorr/src-other/Python-2.5.1
test: LOADLIBES=-L/home/agthorr/src-other/Python-2.5.1 -lpython2.5 -L/bin
test: clean blist.so
	~/src-other/Python-2.5.1/python.exe test_blist.py