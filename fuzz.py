#!/usr/bin/python2.5

import sys
sys.path.append('/home/agthorr/Python-2.5/build/lib.linux-i686-2.5')


from blist import blist
import random, sys

random.seed(3)

type1 = list
type2 = blist
iterations = 100000

lobs = [type1(), type1()]
blobs = [type2(), type2()]

methods = {
    '__add__': 1,
    '__contains__': 1,
    '__delitem__': 1,
    '__delslice__': 2,
    '__eq__': 1,
    '__ge__': 1,
    '__getitem__': 1,
    '__getslice__': 2,
    '__gt__': 1,
    '__hash__': 0,
    '__iadd__': 1,
    '__imul__': 1,
    '__iter__': 1,
    '__le__': 1,
    '__len__': 0,
    '__lt__': 1,
    '__mul__': 1,
    '__ne__': 1,
    '__repr__': 0,
    '__reversed__': 1,
    '__rmul__': 1,
    '__setitem__': 2,
    '__setslice__': 3,
    '__str__': 0,
    'append': 1,
    'count': 1,
    'extend': 1,
    'index': 1,
    'insert': 2,
    'pop': 1,
    'remove': 1,
    'reverse': 0,
    'sort': 3,
    }

left = None
right = None

gen_int = lambda: random.randint(-2**10, 2**10)
gen_float = lambda: random.random()
gen_string = lambda: "foo"
gen_ob_a = lambda: left
gen_ob_b = lambda: right
gen_index_a = lambda: length_left and random.randrange(length_left)
gen_index_b = lambda: length_right and random.randrange(length_right)
gen_cmp = lambda: cmp
gen_key = lambda: (lambda x: x)

gens = [
    gen_int,
    gen_ob_a,
    gen_ob_b,
    gen_index_a,
    gen_index_b,
    gen_cmp,
    gen_key
    ]

def safe_print(s):
    s = str(s)
    print s[:300]

def gen_arg():
    gener = random.sample(gens, 1)[0]
    return gener()

def call(f, args):
    try:
        return f(*args)
    except SystemExit:
        raise
    except KeyboardInterrupt:
        raise
    except BaseException, e:
        return str(type(e))

def smart_eq(a, b, d=None):
    try:
        return a == b
    except RuntimeError:
        pass
    if d is None:
        d = set()
    if id(a) in d and id(b) in d:
        return True
    d.add(id(a))
    d.add(id(b))
    if len(a) != len(b):
        return False
    print len(a),
    sys.stdout.flush()
    if len(a) > 100000:
        print 'skipping',
        sys.stdout.flush()
        return True
    for i in range(len(a)):
        if not smart_eq(a[i], b[i], d):
            return False
    return True

last = None

for _ in range(iterations):
    if not smart_eq(lobs, blobs):
        print
        safe_print(last)
        safe_print('Mismatched objects')
        safe_print(lobs)
        print
        safe_print(blobs)
        break
    print
    if random.random() < 0.5:
        lobs.reverse()
        blobs.reverse()

    left, right = lobs[0], lobs[1]
    length_left = len(left)
    length_right = len(right)

    if random.random() < 0.01 or length_left + length_right > 1000000:
        print '(%d,%d)' % (len(lobs[0]), len(lobs[1]))
        sys.stdout.flush()
        lobs = [type1(), type1()]
        blobs = [type2(), type2()]
        left, right = lobs[0], lobs[1]
        length_left = len(left)
        length_right = len(right)
    else:
        #print '.',
        #sys.stdout.flush()
        pass
    
    method = random.sample(methods, 1)[0]

    args = [gen_arg() for i in range(methods[method])]

    last = ' list: %s%s' % (method, str(tuple(args)))
    print method, '(%d, %d)' % (length_left, length_right), 
    sys.stdout.flush()
    f = getattr(left, method)
    rv1 = call(f, args)
    print '.',
    sys.stdout.flush()

    left, right = blobs[0], blobs[1]
    args2 = []
    for arg in args:
        if arg is lobs[0]:
            args2.append(blobs[0])
        elif arg is lobs[1]:
            args2.append(blobs[1])
        else:
            args2.append(arg)
    #print 'type2: %s%s' % (method, str(tuple(args2)))
    f = getattr(left, method)
    rv2 = call(f, args2)
    print '.',
    sys.stdout.flush()
    if method in ('__repr__', '__str__'):
        continue
    if type(rv1) == type('') and 'MemoryError' in rv1:
        if method.startswith('__i'):
            lobs = [type1(), type1()]
            blobs = [type2(), type2()]
        continue
    if type(rv2) == type(''):
        rv2 = rv2.replace('blist', 'list')
    elif type(rv2) == type2 and random.random() < 0.25:
        blobs[0] = rv2
        lobs[0] = rv1

    if not smart_eq(rv1, rv2):
        if method == 'sort' and length_left == 1:
            continue
        
        print
        safe_print(lobs)
        print
            
        #print last
        safe_print('Mismatched return values')
        safe_print(rv1)
        print
        safe_print(rv2)
        sys.exit(0)

