__version__ = '1.3.6'
from blist._blist import *
import collections
from sys import version_info


_pyvers = version_info.major * 1000 + version_info.minor * 10
# Supported only in Python >= 2.6
if _pyvers >= 2060:
    from blist._sortedlist import sortedlist, sortedset, weaksortedlist, weaksortedset
    from blist._sorteddict import sorteddict
    from blist._btuple import btuple
    if _pyvers >= 3000:
        # Python >= 3
        # collections.MutableSequence works until 3.9 but emits DeprecationWarning
        # This fixes the hard break on 3.9 and the warnings on earlier versions
        import collections.abc
        MutableSequence = collections.abc.MutableSequence
    else:
        # Python >= 2.6, < 3
        MutableSequence = collections.MutableSequence
    MutableSequence.register(blist)
    del _sortedlist, _sorteddict, _btuple
del collections
