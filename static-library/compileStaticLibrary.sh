#!/bin/sh
# Build the static library from the sources of this repository.
set -e
gcc -c -O2 -I../include ../src/NSC.c -o NSC.o
ar rcs NSC.a NSC.o
cp NSC.a NSC.lib   # The *.a file is simply renamed *.lib for the Windows toolchains
rm -f NSC.o
echo "Static library built : NSC.a and NSC.lib"
