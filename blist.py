from _blist import *
import collections
if hasattr(collections, 'MutableSet'): # Only supported in Python 2.6+
    from _sortedlist import *
del collections
