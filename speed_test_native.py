#!/usr/bin/python2.5

import os, sys, subprocess
from math import *

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

setup = ''

types = ('blist', 'list')

typemap = {
    'blist': '/home/agthorr/mypython-2.5/python',
    'list': '/home/agthorr/Python-2.5/python',
}

ns = (range(1,10) + range(10, 100, 10) + range(100, 1000, 100)
      + range(1000, 10001, 1000))

def smart_timeit(python, stmt, setup, hint):
    n = hint
    while 1:
        v = timeit(python, stmt, setup, n)
        if v[0]*n > MIN_TIME:
            return v, n
        n <<= 1

timeit_cache = {}
def timeit(python, stmt, setup, rep):
    assert rep >= MIN_REPS
    key = (python, stmt, setup, rep)
    if key in timeit_cache:
        return timeit_cache[key]
    try:
        n = 9
        p = subprocess.Popen([python, '/usr/lib/python2.5/timeit.py',
                              '-r', str(n), '-v', '-n', str(rep), '-s', setup, '--', stmt],
                             stdout=subprocess.PIPE)
        so, se = p.communicate()
        try:
            lines = so.split('\n')

            raw = lines[0]
            number = int(lines[1].split()[0])
            times = [float(x) / number for x in raw.split()[2:]]
            times.sort()

            v = (times[n//2+1], times[n//4+1], times[(3*n)//4+1])

            #so = lines[1]
            #parts = so.split()
            #v = float(parts[-4])
            #units = parts[-3]
            #if units == 'usec':
            #    v *= 10.0**-6
            #elif units == 'msec':
            #    v *= 10.0**-3
            #elif units == 'sec':
            #    pass
            #else:
            #    raise 'Unknown units'
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
def get_timing1(label, setup_n, template, typename, use_rep_map):
    f = open('dat/%s-%s.dat' % (str(typename), label), 'w')
    print >>f, '#', label
    print >>f, '#', template.replace('\n', '\\n')
    for i in reversed(range(len(ns))):
        n = ns[i]
        key = (label, setup_n, n, template, typename)
        print n,
        sys.stdout.flush()
        setup2 = '\nn = %d\n' % (n,)
        if setup_n is None:
            setup3 = "x = list(range(n))"
        else:
            setup3 = setup_n
        setup3 = setup + '\n' + setup2 + setup3
        stmt = template
        if not use_rep_map:
            if i < len(ns)-1:
                rep_map[n] = max(rep_map[n], rep_map[ns[i+1]])
            v, rep = smart_timeit(typemap[typename], stmt, setup3, rep_map[n])
            if rep_map[n] < rep:
                rep_map[n] = rep
        else:
            k = rep_map[n]
            if k * values[key] > MAX_TIME:
                k = max(MIN_REPS, int(ceil(MAX_TIME / values[key])))
            v = timeit(typemap[typename], stmt, setup3, k)
        values[key] = v[0]
        v = [x*1000 for x in v]
        if typename == 'list':
            list_values[n] = v[0]
        print >>f, n,
        for x in v:
            print >>f, x,
        for x in v:
            print >>f, x/list_values[n],
        print >>f
    print
    f.close()

def get_timing(label, setup_n, template):
    global rep_map, list_values
    rep_map = {}
    list_values = {}
    for n in ns:
        rep_map[n] = MIN_REPS
    get_timing1(label, setup_n, template, 'list', False)
    print 'Timing', label, ':',
    sys.stdout.flush()
    get_timing1(label, setup_n, template, 'blist', False)

    get_timing1(label, setup_n, template, 'list', True)
    print 'Timing', label, ':',
    sys.stdout.flush()
    get_timing1(label, setup_n, template, 'blist', True)

    plot(label, True)
    plot(label, False)
    html(label)

def html(label):
    fname = 'fig/%s.html' % label
    f = open(fname, 'w')
    if timing_d[label][0] is None:
        setup = 'x = list(range(n))'
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
#set bmargin 3

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

#set key below height 1
""" % (d, label, safe_label)

    if relative:
        print >>f, 'set title "Normalized Execution Times, log-linear scale"'
        print >>f, 'set logscale x'
        print >>f, 'set yrange [0:*]'
        print >>f, 'set yrange [0:200]'
        print >>f, 'set ylabel "Execution Time (%)"'
        print >>f, 'set key bottom left'
        print >>f, 'set mytics 5'
    else:
        print >>f, 'set title "Raw Execution Times, log-log scale"'
        print >>f, 'set key top left'
        #print >>f, 'set mytics 10'
        print >>f, 'set logscale xy'
        print >>f, 'set yrange [0.0001:10]'
        print >>f, 'set ylabel "Execution Time"'
        print >>f, 'set ytics ("1 ns" 0.000001, "10 ns" 0.00001, "100 ns" 0.0001, "1 us" 0.001, "10 us" 0.01, "100 us" 0.1, "1 ms" 1.0, "10 ms" 10.0, "100 ms" 100.0)'

    if relative:
        k = 3
        m = 100.0
    else:
        k = 0
        m = 1.0

    print >>f, ('plot "dat/list-%s.dat" using 1:(%f*$%d):(%f*$%d):(%f*$%d) title "list()" with yerrorlines, \\'
                % (label, m, k+2, m, k+3, m, k+4))
    print >>f, ('    "dat/blist-%s.dat" using 1:(%f*$%d):(%f*$%d):(%f*$%d) title "blist()" with yerrorlines '
                % (label, m, k+2, m, k+3, m, k+4))
        
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
#   2) setup code to run once.  "None" means x = list(range(n))
#   3) code to execute repeatedly in a loop
#
# The following symbols will automatically be defined:
#   - n

add_timing('FIFO', None, """\
x.insert(0, 0)
del x[0]
""")

add_timing('LIFO', None, """\
x.append(0)
del x[-1]
""")

add_timing('add', None, "x + x")
add_timing('contains', None, "-1 in x")
add_timing('getitem1', None, "x[0]")
add_timing('getitem2', None, "x.__getitem__(0)")
add_timing('getitem3', 'x = range(n)\nm = n//2', "x[m]")
add_timing('getslice', None, "x[1:-1]")
add_timing('forloop', None, "for i in x:\n    pass")
add_timing('len', None, "len(x)")
add_timing('eq', None, "x == x")
add_timing('mul10', None, "x * 10")
add_timing('setitem1', None, 'x[0] = 1')
add_timing('setitem3', 'x = range(n)\nm = n//2', 'x[m] = 1')
add_timing('count', None, 'x.count(5)')
add_timing('reverse', None, 'x.reverse()')
add_timing('delslice', None, 'del x[len(x)//4:3*len(x)//4]\nx *= 2')
add_timing('setslice', None, 'x[:] = x')

add_timing('sort random', 'import random\nx = [random.random() for i in range(n)]', 'y = list(x)\ny.sort()')
add_timing('sort sorted', None, 'y = list(x)\ny.sort()')
add_timing('sort reversed', 'x = range(n)\nx.reverse()', 'y = list(x)\ny.sort()')

add_timing('init from list', 'x = range(n)', 'y = list(x)')
add_timing('init from tuple', 'x = tuple(range(n))', 'y = list(x)')
add_timing('init from iterable', 'x = xrange(n)', 'y = list(x)')
add_timing('init from same type', None, 'y = list(x)')

add_timing('shuffle', 'from random import shuffle\nx = list(range(n))', 'shuffle(x)')

if __name__ == '__main__':
    if len(sys.argv) == 1:
        run_all()
    else:
        for name in sys.argv[1:]:
            run_timing(name)
