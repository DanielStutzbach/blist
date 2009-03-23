#!/usr/bin/python2.5

import os, sys, subprocess
from math import *

# Set these to match the options used to compile Python.  Otherwise,
# you won't get a fair comparison with Python's built-in list type
#CFLAGS = '-g -O3 -DNDEBUG=1 -DLIMIT=%d -fno-strict-aliasing -I/usr/include/python2.5 -Winline'# --param inline-unit-growth=2000 --param max-inline-insns-single=2000 --param max-inline-insns-auto=2000' # --param inline-unit-growth=2000'
#CFLAGS='-pg -O3 -DLIMIT=%d -fno-strict-aliasing -DNDEBUG=1 -I/usr/include/python2.5'
CFLAGS='-c -fno-strict-aliasing -DNDEBUG -g -O3 -Wall -Wstrict-prototypes -I/usr/include/python2.5 --std=gnu99'
CC = 'gcc-4.3'
PYTHON='python2.5'
LD = CC
LDFLAGS='-g -shared'
LOADLIBES='-lpython2.5'
#PYTHON='/home/agthorr/mypython-2.5/python'

# List of BList node sizes to try
#limits = (8, 64, 128, 512, 2048)
#limits = (8, 128)
limits = (128, )

# The tests to run are near the bottom

MIN_REPS = 3
MIN_TIME = 0.01
MAX_TIME = 1.0

if 'cygwin' in os.uname()[0].lower():
    extension = 'dll'
else:
    extension = 'so'

def makedir(x):
    try:
        os.mkdir(x)
    except OSError:
        pass

def rm(x):
    try:
        os.unlink(x)
    except OSError:
        pass

makedir('fig')
makedir('fig/relative')
makedir('fig/absolute')
makedir('.cache')
makedir('dat')
makedir('gnuplot')

make_cache = set()
current_limit = None
def make(limit):
    global current_limit
    current_limit = limit
    if limit == 'list': return
    if limit in make_cache:
        os.system('cp .cache/blist.%s-%d blist.%s' % (extension, limit, extension))
        return
    if os.system('python2.5 setup.py clean -a > /dev/null 2> /dev/null'):
        raise 'Make failure'
    rm('blist.%s' % extension)
    rm('blist.o')
    if os.system('%s %s -DLIMIT=%d -c blist.c -o blist.o' % (CC, CFLAGS, limit)):
        raise 'Make failure'
    if os.system('%s %s -o blist.so blist.o %s' % (LD, LDFLAGS, LOADLIBES)):
        raise 'Make failure'
    #if os.system('CC="%s" python2.5 setup.py build --build-platlib .' % (CC % limit)):
    #    raise 'Make failure'
    #if os.system('make CFLAGS="%s"' % (CFLAGS % limit)):
    #    raise 'Make failure'
    os.system('cp blist.%s .cache/blist.%s-%d' % (extension, extension, limit))
    make_cache.add(limit)

setup = 'from blist import blist'

ns = (range(1,10) + range(10, 100, 10) + range(100, 1000, 100)
      + range(1000, 10001, 1000)
      + range(10000, 100001, 10000))

def smart_timeit(stmt, setup, hint):
    n = hint
    while 1:
        v = timeit(stmt, setup, n)
        if v*n > MIN_TIME:
            return v, n
        n <<= 1

timeit_cache = {}
def timeit(stmt, setup, rep):
    assert rep >= MIN_REPS
    key = (stmt, setup, rep, current_limit)
    if key in timeit_cache:
        return timeit_cache[key]
    try:
        p = subprocess.Popen([PYTHON, '/usr/lib/python2.5/timeit.py',
                              '-r', '5', '-n', str(rep), '-s', setup, '--', stmt],
                             stdout=subprocess.PIPE)
        so, se = p.communicate()
        try:
            parts = so.split()
            v = float(parts[-4])
            units = parts[-3]
            if units == 'usec':
                v *= 10.0**-6
            elif units == 'msec':
                v *= 10.0**-3
            elif units == 'sec':
                pass
            else:
                raise 'Unknown units'
            timeit_cache[key] = v
            return v
        except:
            print so
            print se
            raise
    except:
        print stmt
        print setup
        raise

values = {}
def get_timing1(limit, label, setup_n, template, typename, use_rep_map):
    f = open('dat/%s-%s.dat' % (str(limit), label), 'w')
    print >>f, '#', label
    print >>f, '#', template.replace('\n', '\\n')
    for i in reversed(range(len(ns))):
        n = ns[i]
        key = (limit, label, setup_n, n, template, current_limit)
        print n,
        sys.stdout.flush()
        setup2 = '\nTypeToTest = %s\nn = %d\n' % (typename, n)
        if setup_n is None:
            setup3 = "x = TypeToTest(range(n))"
        else:
            setup3 = setup_n
        setup3 = setup + '\n' + setup2 + setup3
        stmt = template
        if not use_rep_map:
            if i < len(ns)-1:
                rep_map[n] = max(rep_map[n], rep_map[ns[i+1]])
            v, rep = smart_timeit(stmt, setup3, rep_map[n])
            if rep_map[n] < rep:
                rep_map[n] = rep
        else:
            k = rep_map[n]
            if k * values[key] > MAX_TIME:
                k = max(MIN_REPS, int(ceil(MAX_TIME / values[key])))
            v = timeit(stmt, setup3, k)
        values[key] = v
        v *= 1000
        if limit == 'list':
            list_values[n] = v
            print >>f, n, v
        else:
            print >>f, n, v, v/list_values[n]
    print
    f.close()

def get_timing(label, setup_n, template):
    global rep_map, list_values
    rep_map = {}
    list_values = {}
    for n in ns:
        rep_map[n] = MIN_REPS
    make('list')
    get_timing1('list', label, setup_n, template, 'list', False)
    for limit in limits:
        print 'Timing', label, limit, ':',
        sys.stdout.flush()
        make(limit)
        get_timing1(limit, label, setup_n, template, 'blist', False)

    make('list')
    get_timing1('list', label, setup_n, template, 'list', True)
    for limit in limits:
        print 'Timing', label, limit, ':',
        sys.stdout.flush()
        make(limit)
        get_timing1(limit, label, setup_n, template, 'blist', True)

    plot(label, True)
    plot(label, False)
    html(label)

def html(label):
    fname = 'fig/%s.html' % label
    f = open(fname, 'w')
    if timing_d[label][0] is None:
        setup = 'x = TypeToTest(range(n))'
    else:
        setup = timing_d[label][0]
    print >>f, '''
<html>
<head>
<title>BList vs Python list timing results: %s</title>
</head>
<body>
<div style="width: 100%%; background-color: #ccc;">
<a href="/">Home</a>
| <a href="/blist/">BList</a>
| <a href="http://pokersleuth.com/">Poker Sleuth</a>
| <a href="http://pokersleuth.com/poker-crunch.shtml">Poker Calculator</a>
| <a href="http://pokersleuth.com/hand-converter.shtml">Hand Converter</a>

</div>
    
<img src="absolute/%s.png"/>
<img src="relative/%s.png"/>
<p>
Setup:
<pre>
%s
</pre>
Timed:
<pre>
%s
</pre>
</body>
</html>
    ''' % (label, label, label, setup, timing_d[label][1])
    f.close()

def plot(label, relative):
    safe_label = label.replace('_', '\\\\_')
    fname = 'gnuplot/%s.gnuplot' % label
    f = open(fname, 'w')
    if relative:
        d = 'fig/relative/'
    else:
        d = 'fig/absolute/'
    os.putenv('GDFONTPATH', '/usr/share/fonts/truetype/msttcorefonts/')
    print >>f, """
set output "%s/%s.png"
set xlabel "List Size (n)"
set title "%s"
set bmargin 3

#set pointsize 2
#set view 60, 30, 1.0, 1.0
#set lmargin 12
#set rmargin 10
#set tmargin 1
#set bmargin 5
#set ylabel 0
#set mxtics default
#set mytics default
#set tics out
#set nox2tics
#set noy2tics
#set border 3
#set xtics nomirror autofreq
#set ytics nomirror autofreq
#set key height 1
#set nokey
#unset xdata
#unset y2label
#unset x2label

#set format "%%g"
set terminal png transparent interlace medium font "./times.ttf" size 640,480 nocrop enhanced xffffff x000000 xff0000 x0000ff xc030c0 xff0000 x000000
set size noratio 1,1

set key below height 1
""" % (d, label, safe_label)

    if relative:
        print >>f, 'set title "Normalized Execution Times, log-linear scale"'
        print >>f, 'set logscale x'
        print >>f, 'set yrange [0:*]'
        print >>f, 'set yrange [0:200]'
        print >>f, 'set ylabel "Execution Time (%)"'
        print >>f, 'set key bottom left'
        print >>f, 'set mytics 5'
        print >>f, 'plot 100 title "list()" ',
    else:
        print >>f, 'set title "Raw Execution Times, log-log scale"'
        print >>f, 'set key top left'
        print >>f, 'set mytics 10'
        print >>f, 'set logscale xy'
        print >>f, 'set yrange [0.0001:10]'
        print >>f, 'set ylabel "Execution Time"'
        print >>f, 'set ytics ("100 ns" 0.0001, "1 us" 0.001, "10 us" 0.01, "100 us" 0.1, "1 ms" 1.0, "10 ms" 10.0, "100 ms" 100.0)'
        print >>f, 'plot "dat/list-%s.dat" title "list()" with linespoints ' \
              % (label),
    for limit in limits:
        print >>f, ', \\'
        if relative:
            print >>f, '     "dat/%d-%s.dat" using 1:(100.0*$3) title "blist(), limit=%d" with linespoints' \
                  % (limit, label, limit),
        else:
            print >>f, '     "dat/%d-%s.dat" using 1:($2) title "blist(), limit=%d" with linespoints' \
                  % (limit, label, limit),
    print >>f
    f.flush()
    f.close()
    if os.system('gnuplot "%s"' % fname):
        raise 'Gnuplot failure'

timing_d = {}
def add_timing(name, auto, stmt):
    timing_d[name] = (auto, stmt)

def run_timing(name):
    auto, stmt = timing_d[name]
    get_timing(name, auto, stmt)

def run_all():
    for k in sorted(timing_d):
        run_timing(k)

########################################################################
# Tests to run are below here.
# The arguments to add_timing are as follows:
#   1) name of the test
#   2) setup code to run once.  "None" means x = TypeToTest(range(n))
#   3) code to execute repeatedly in a loop
#
# The following symbols will autoamtically be defined:
#   - blist
#   - TypeToTest
#   - n

#add_timing('eq list', 'x = TypeToTest(range(n))\ny=range(n)', 'x==y')
add_timing('eq recursive', 'x = TypeToTest()\nx.append(x)\ny = TypeToTest()\ny.append(y)', 'try:\n  x==y\nexcept RuntimeError:\n  pass')

#add_timing('FIFO', None, """\
#x.insert(0, 0)
#del x[0]
#""")
#
#add_timing('LIFO', None, """\
#x.append(0)
#del x[-1]
#""")
#
#add_timing('add', None, "x + x")
#add_timing('contains', None, "-1 in x")
add_timing('getitem1', None, "x[0]")
add_timing('getitem2', None, "x.__getitem__(0)")
add_timing('getitem3', 'x = TypeToTest(range(n))\nm = n//2', "x[m]")
#add_timing('getslice', None, "x[1:-1]")
#add_timing('forloop', None, "for i in x:\n    pass")
#add_timing('len', None, "len(x)")
#add_timing('eq', None, "x == x")
#add_timing('mul10', None, "x * 10")
add_timing('setitem1', None, 'x[0] = 1')
add_timing('setitem3', 'x = TypeToTest(range(n))\nm = n//2', 'x[m] = 1')
#add_timing('count', None, 'x.count(5)')
#add_timing('reverse', None, 'x.reverse()')
#add_timing('delslice', None, 'del x[len(x)//4:3*len(x)//4]\nx *= 2')
#add_timing('setslice', None, 'x[:] = x')
#
#add_timing('sort random', 'import random\nx = [random.random() for i in range(n)]', 'y = TypeToTest(x)\ny.sort()')
#add_timing('sort sorted', None, 'y = TypeToTest(x)\ny.sort()')
#add_timing('sort reversed', 'x = range(n)\nx.reverse()', 'y = TypeToTest(x)\ny.sort()')
#
#add_timing('init from list', 'x = range(n)', 'y = TypeToTest(x)')
#add_timing('init from tuple', 'x = tuple(range(n))', 'y = TypeToTest(x)')
#add_timing('init from iterable', 'x = xrange(n)', 'y = TypeToTest(x)')
#add_timing('init from same type', None, 'y = TypeToTest(x)')
#
#add_timing('shuffle', 'from random import shuffle\nx = TypeToTest(range(n))', 'shuffle(x)')

if __name__ == '__main__':
    make(limits[0])
    if len(sys.argv) == 1:
        run_all()
    else:
        for name in sys.argv[1:]:
            run_timing(name)
