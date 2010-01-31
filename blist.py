from _blist import *
import collections
if hasattr(collections, 'MutableSet'): # Only supported in Python 2.6+
    from _sortedlist import sortedlist, sortedset, weaksortedlist, weaksortedset
    from _sorteddict import sorteddict
    from _btuple import btuple
del collections
