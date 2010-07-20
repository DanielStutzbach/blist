#!/usr/bin/python
from __future__ import print_function

import os, sys, subprocess
from math import *

# The tests to run are near the bottom

MIN_REPS = 3
NUM_POINTS = 9
MIN_TIME = 0.1
MAX_TIME = 1.0
MAX_X = 100000

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

limits = (128,)
current_limit = None
def make(limit):
    global current_limit
    current_limit = limit

setup = 'from blist import blist'

ns = []
for i in range(50+1):
    ns.append(int(floor(10**(i*0.1))))
ns = list(i for i in sorted(set(ns)) if i <= MAX_X)

def smart_timeit(stmt, setup, hint):
    n = hint
    while 1:
        v = timeit(stmt, setup, n)
        if v[0]*n > MIN_TIME:
            return v, n
        n <<= 1

import timeit
timeit_path = timeit.__file__

timeit_cache = {}
def timeit(stmt, setup, rep):
    assert rep >= MIN_REPS
    key = (stmt, setup, rep, current_limit)
    if key in timeit_cache:
        return timeit_cache[key]
    try:
        n = NUM_POINTS
        args =[sys.executable, timeit_path,
               '-r', str(n), '-v', '-n', str(rep), '-s', setup, '--', stmt]
        p = subprocess.Popen(args,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        so, se = p.communicate()
        try:
            lines = so.split(b'\n')
            raw = lines[0]
            number = int(lines[1].split()[0])
            times = [float(x) / number for x in raw.split()[2:]]
            times.sort()
            # median, lower quartile, upper quartile
            v = (times[n//2], times[n//4], times[3*n//4])
            timeit_cache[key] = v
            return v
        except:
            print(so)
            print(se)
            raise
    except:
        print(stmt)
        print(setup)
        raise

values = {}
def get_timing1(limit, label, setup_n, template, typename, use_rep_map):
    f = open('dat/%s-%s.dat' % (str(limit), label), 'w')
    print('#', label, file=f)
    print('#', template.replace('\n', '\\n'), file=f)
    if setup_n is None:
        setup_n = "x = TypeToTest(range(n))"
    else:
        setup_n = setup_n
    ftimeit = open('fig/%s.txt' % label, 'w')
    print('<div class="blist_inner">Setup: <code>%s</code><br/>' % setup_n.replace('\n', '<br/>'), 
          file=ftimeit)
    print('Timed: <code>%s</code></div>' % template.replace('\n', '<br/>'), 
          file=ftimeit)
    ftimeit.close()
 
    for i in reversed(list(range(len(ns)))):
        n = ns[i]
        key = (limit, label, setup_n, n, template, typename)
        print(n, end=' ')
        sys.stdout.flush()
        setup2 = '\nTypeToTest = %s\nn = %d\n' % (typename, n)
        setup3 = setup + '\n' + setup2 + setup_n
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
        values[key] = v[0]
        v = [x*1000 for x in v]
        if typename == 'list':
            list_values[n] = v[0]
        print(n, file=f, end=' ')
        for x in v:
            print(x, file=f, end=' ')
        for x in v:
            print(x/list_values[n], file=f, end=' ')
        print(file=f)
    print()
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
        print('Timing', label, limit, ':', end=' ')
        sys.stdout.flush()
        make(limit)
        get_timing1(limit, label, setup_n, template, 'blist', False)

    make('list')
    get_timing1('list', label, setup_n, template, 'list', True)
    for limit in limits:
        print('Timing', label, limit, ':', end=' ')
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
    print('''
<html>
<head>
<title>BList vs Python list timing results: %s</title>
<script src="svg.js"></script>
</head>
<body>
<div style="width: 100%%; background-color: #ccc;">
<a href="/">Home</a>
| <a href="/blist/">BList</a>
| <a href="http://pokersleuth.com/">Poker Sleuth</a>
| <a href="http://pokersleuth.com/poker-crunch.shtml">Poker Calculator</a>
| <a href="http://pokersleuth.com/hand-converter.shtml">Hand Converter</a>

</div>
    
<object data="absolute/%s.svg" width="480" height="360"
	type="image/svg+xml"></object>
<object data="relative/%s.svg" width="480" height="360"
	type="image/svg+xml"></object>
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
    ''' % (label, label, label, setup, timing_d[label][1]), file=f)
    f.close()

def plot(label, relative):
    safe_label = label.replace('_', '\\\\_')
    fname = 'gnuplot/%s.gnuplot' % label
    f = open(fname, 'w')
    if relative:
        d = 'fig/relative/'
    else:
        d = 'fig/absolute/'
    print("""
set output "%s/%s.svg"
set xlabel "List Size (n)"
set title "%s"
set terminal svg size 480,360 dynamic enhanced
set size noratio 1,1
set key top left
set bars 0.2
set pointsize 0.5
set xtics ("1" 1, "10" 10, "100" 100, "1k" 1000, "10k" 10000, "100k" 100000, "1M" 1000000)
""" % (d, label, safe_label), file=f)

    if relative:
        print('set title "Normalized Execution Times, log-linear scale"', file=f)
        print('set logscale x', file=f)
        print('set yrange [0:*]', file=f)
        print('set yrange [0:200]', file=f)
        print('set ylabel "Execution Time (%)"', file=f)
        k = 3
        m = 100.0
    else:
        print('set title "Raw Execution Times, log-log scale"', file=f)
        print('set logscale xy', file=f)
        print('set yrange [0.00001:10]', file=f)
        print('set ylabel "Execution Time"', file=f)
        print('set ytics ("1 ns" 0.000001, "10 ns" 0.00001, "100 ns" 0.0001, "1 us" 0.001, "10 us" 0.01, "100 us" 0.1, "1 ms" 1.0, "10 ms" 10.0, "100 ms" 100.0)', file=f)
        k = 0
        m = 1.0

    print (('plot "dat/list-%s.dat" using 1:(%f*$%d):(%f*$%d):(%f*$%d) title "list()" with yerr pt 1, \\' % (label, m, k+2, m, k+3, m, k+4)), file=f)
    for limit in limits:
        print (('    "dat/%d-%s.dat" using 1:(%f*$%d):(%f*$%d):(%f*$%d) title "blist()" with yerr pt 1 '% (limit, label, m, k+2, m, k+3, m, k+4)), file=f)
    print(file=f)
    f.flush()
    f.close()
    if os.system('gnuplot "%s"' % fname):
        raise RuntimeError('Gnuplot failure')

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

add_timing('eq list', 'x = TypeToTest(range(n))\ny=range(n)', 'x==y')
#add_timing('eq recursive', 'x = TypeToTest()\nx.append(x)\ny = TypeToTest()\ny.append(y)', 'try:\n  x==y\nexcept RuntimeError:\n  pass')

add_timing('FIFO', None, """\
x.insert(0, 0)
x.pop(0)
""")

add_timing('LIFO', None, """\
x.append(0)
x.pop(-1)
""")

add_timing('add', None, "x + x")
add_timing('contains', None, "-1 in x")
#add_timing('getitem1', None, "x[0]")
#add_timing('getitem2', None, "x.__getitem__(0)")
add_timing('getitem3', 'x = TypeToTest(range(n))\nm = n//2', "x[m]")
add_timing('getslice', None, "x[1:-1]")
add_timing('forloop', None, "for i in x:\n    pass")
add_timing('len', None, "len(x)")
add_timing('eq', None, "x == x")
add_timing('mul10', None, "x * 10")
#add_timing('setitem1', None, 'x[0] = 1')
add_timing('setitem3', 'x = TypeToTest(range(n))\nm = n//2', 'x[m] = 1')
add_timing('count', None, 'x.count(5)')
add_timing('reverse', None, 'x.reverse()')
add_timing('delslice', None, 'del x[len(x)//4:3*len(x)//4]\nx *= 2')
add_timing('setslice', None, 'x[:] = x')

add_timing('sort random', 'import random\nx = [random.randrange(n*4) for i in range(n)]', 'y = TypeToTest(x)\ny.sort()')
add_timing('sort random key', 'import random\nx = [random.randrange(n*4) for i in range(n)]', 'y = TypeToTest(x)\ny.sort(key=float)')
add_timing('sort sorted', None, 'x.sort()')
add_timing('sort sorted key', None, 'x.sort(key=int)')
add_timing('sort reversed', 'x = list(range(n))\nx.reverse()', 'y = TypeToTest(x)\ny.sort()')
add_timing('sort reversed key', 'x = list(range(n))\nx.reverse()', 'y = TypeToTest(x)\ny.sort(key=int)')

add_timing('sort random tuples', 'import random\nx = [(random.random(), random.random()) for i in range(n)]', 'y = TypeToTest(x)\ny.sort()')

ob_def = '''
import random
class ob:
    def __init__(self, v):
        self.v = v
    def __lt__(self, other):
        return self.v < other.v
x = [ob(random.randrange(n*4)) for i in range(n)]
'''

add_timing('sort random objects', ob_def, 'y = TypeToTest(x)\ny.sort()')
add_timing('sort sorted objects', ob_def + 'x.sort()', 'x.sort()')

add_timing('init from list', 'x = list(range(n))', 'y = TypeToTest(x)')
add_timing('init from tuple', 'x = tuple(range(n))', 'y = TypeToTest(x)')
add_timing('init from iterable', 'x = range(n)', 'y = TypeToTest(x)')
add_timing('init from same type', None, 'y = TypeToTest(x)')

add_timing('shuffle', 'from random import shuffle\nx = TypeToTest(range(n))', 'shuffle(x)')

if __name__ == '__main__':
    make(128)
    if len(sys.argv) == 1:
        run_all()
    else:
        for name in sys.argv[1:]:
            run_timing(name)
