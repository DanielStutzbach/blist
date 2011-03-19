#!/usr/bin/env python

import sys
import distribute_setup
distribute_setup.use_setuptools()
from setuptools import setup, Extension

define_macros = []

import ctypes
if ctypes.sizeof(ctypes.c_double) == 8:
    dv = ctypes.c_double(9006104071832581.0)
    iv = ctypes.cast(ctypes.pointer(dv), ctypes.POINTER(ctypes.c_uint64))
    if iv.contents.value == 0x433fff0102030405:
        define_macros.append(('BLIST_FLOAT_RADIX_SORT', 1))

setup(name='blist',
      version='1.3.4',
      description='a list-like type with better asymptotic performance and similar performance on small lists',
      author='Stutzbach Enterprises, LLC',
      author_email='daniel@stutzbachenterprises.com',
      url='http://stutzbachenterprises.com/blist/',
      license = "BSD",
      keywords = "blist list b+tree btree fast copy-on-write sparse array sortedlist sorted sortedset weak weaksortedlist weaksortedset sorteddict btuple",
      ext_modules=[Extension('_blist', ['_blist.c'],
                             define_macros=define_macros,
                             )],
      provides = ['blist'],
      py_modules=['blist', '_sortedlist', '_sorteddict', '_btuple'],
      test_suite = "test_blist.test_suite",
      zip_safe = False, # zips are broken on cygwin for C extension modules
      classifiers = [
            'Development Status :: 5 - Production/Stable',
            'Intended Audience :: Developers',
            'Intended Audience :: Science/Research',
            'License :: OSI Approved :: BSD License',
            'Programming Language :: C',
            'Programming Language :: Python :: 2.5',
            'Programming Language :: Python :: 2.6',
            'Programming Language :: Python :: 2.7',
            'Programming Language :: Python :: 3',
            'Programming Language :: Python :: 3.1',
            'Programming Language :: Python :: 3.2',
            ],
      long_description=open('README.rst').read()
)
