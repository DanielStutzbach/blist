__version__ = '1.3.6'
from blist._blist import *
import collections
if hasattr(collections, 'MutableSet'): # Only supported in Python 2.6+
    from blist._sortedlist import sortedlist, sortedset, weaksortedlist, weaksortedset
    from blist._sorteddict import sorteddict
    from blist._btuple import btuple
    collections.MutableSequence.register(blist)
    del _sortedlist, _sorteddict, _btuple
del collections
