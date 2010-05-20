weaksortedlist
==============

.. currentmodule:: blist

.. class:: weaksortedlist(iterable=(), key=None)

   A :class:`weaksortedlist` provides exactly the same methods and has
   the same performance characteristics as a :class:`sortedlist`.
   However, it keeps a `weak reference
   <http://docs.python.org/library/weakref.html>`_ to its members
   instead of a strong reference.  After an item has no more strong
   references to it, the item will be removed from the list.

