PYTHON=python2.5
PYPREFIX=/usr

CFLAGS=-Wall -g -fno-strict-aliasing -Wstrict-prototypes $(COPT) -Werror $(INCLUDE)
#INCLUDE=-I/home/agthorr/Python-3.0/Include -I/home/agthorr/Python-3.0/ #-I/home/agthorr/mypython-3.0/Include/ -I/home/agthorr/mypython-3.0/
INCLUDE=-I$(PYPREFIX)/include/$(PYTHON)
CC=gcc #-L/home/agthorr/Python-3.0

#COPT=-O3 -DLIMIT=128 -DNDEBUG=1 # For performance
#COPT=-fno-inline -pg -O3 -DLIMIT=128 -DNDEBUG=1# -ftest-coverage -fprofile-arcs # For profiling
COPT=-DLIMIT=8 -DPy_DEBUG=1     # For debug mode

LDFLAGS=-g -shared $(COPT)
DLLFLAGS=-Wl,--enable-auto-image-base 
LD=$(CC)
LOADLIBES=-L$(PYPREFIX)/lib -L$(PYPREFIX)/lib/$(PYTHON)/config/ -l$(PYTHON)

blist.dll: blist.o
	$(LD) $(LDFLAGS) $(DLLFLAGS) -o $@ $< $(LOADLIBES)

blist.so: blist.o
	$(LD) $(LDFLAGS) -o $@ $< $(LOADLIBES)

clean:
	rm -f *.o *.so *.dll

tarball:
	cd .. ; tar -zcvf blist.tar.gz blist/blist.c blist/Makefile blist/test_blist.py blist/test/*.py blist/prototype/blist.py blist/fuzz.py blist/README.txt blist/blist.rst blist/blist.h blist/LICENSE blist/CREDITS blist/setup.py

egg:
	$(PYTHON) setup.py register
	CFLAGS='-O3 -fno-strict-aliasing' $(PYTHON) setup.py build -f
	rm -f dist/*.asc
	$(PYTHON) setup.py sdist bdist_egg upload -s
	rsync -e ssh dist/* webadmin@stutzbachenterprises.com:stutzbachenterprises/html/blist/

bdist_egg:
	CFLAGS='-O3 -fno-strict-aliasing' $(PYTHON) setup.py build -f
	rm -f dist/*.asc
	$(PYTHON) setup.py bdist_egg upload -s

html:
	rst2html README.txt

speed:
	$(PYTHON) speed_test.py 
	rsync -e ssh -r fig/* webadmin@stutzbachenterprises.com:stutzbachenterprises/html/fig/

test: COPT=-DLIMIT=8 -DPy_DEBUG=1 
test: LOADLIBES=-l$(PYTHON) -L/bin
test: clean blist.so
	$(PYTHON)-dbg test_blist.py

testing: test.o blist.o
	$(LD) -pthread -pg -Xlinker -export-dynamic -I/home/agthorr/Python-3.0/Include -I/home/agthorr/Python-3.0/ test.o blist.o -L/home/agthorr/Python-3.0 -l$(PYTHON) -o testing -lpthread -ldl -lutil -lm

win:
	/cygdrive/c/Python26/python.exe setup.py bdist_wininst
	/cygdrive/c/Python30/python.exe setup.py bdist_wininst
	gpg --detach-sign -a dist/blist-$(VERSION).win32-py2.6.exe
	gpg --detach-sign -a dist/blist-$(VERSION).win32-py3.0.exe
