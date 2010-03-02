#!/usr/bin/env python

import os, sys

names = set()
for fname in os.listdir('dat/'):
    if fname.endswith('.dat'):
        names.add(fname.split('.')[0].split('-')[1])

if len(sys.argv) > 1:
    names = set(sys.argv[1:])

final = {}
for name in names:
    data = {}
    with open('dat/128-%s.dat' % name) as f:
        lines = f.readlines()
    for line in lines:
        if line[0] == '#':
            continue
        line = line.split()
        if len(line) < 5:
            continue
        data[int(line[0])] = float(line[4])
    if not data:
        continue
    final[name] = sum(data.values())/len(data)

items = [(t[1], t[0]) for t in final.items()]
items.sort()
hit1 = len(sys.argv) > 1
for v, name in items:
    if v >= 1.0 and not hit1:
        hit1 = True
        print '-'*72
    print '%.0f%% %s' % (v*100, name)

     
