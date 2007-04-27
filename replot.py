#!/usr/bin/python

from speed_test import *

if len(sys.argv) == 1:
    for k in timing_d:
        plot(k, True)
        plot(k, False)
        html(k)
else:
    name = sys.argv[1]
    plot(name, True)
    plot(name, False)
    html(name)
