# -*- coding: utf-8 -*-

from setuptools import setup, find_packages, Extension
from codecs import open
from os import path

here = path.abspath(path.dirname(__file__))

# Get the long description from the relevant file
with open(path.join(here, 'DESCRIPTION.rst'), encoding='utf-8') as f:
    long_description = f.read()

setup(
    name='socketextra',
    version='0.0.3',
    description='sendmsg etc. for Python2 (not needed for Py3)',
    long_description=long_description,

    url='https://github.com/Bluehorn/socketextra',
    author='Torsten Landschoff',
    author_email='t.landschoff@gmx.net',

    license='MIT',

    # See https://pypi.python.org/pypi?%3Aaction=list_classifiers
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'Topic :: System :: Networking',
        'License :: OSI Approved :: MIT License',

        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.6',
        'Programming Language :: Python :: 2.7',
    ],

    keywords='socket sendmsg recvmsg',
    packages=find_packages(exclude=['contrib', 'docs', 'tests*']),

    install_requires=[],
    extras_require={},
    ext_modules=[Extension('socketextra._socketextra', ["socketextra/_socketextra.c"])],
)
