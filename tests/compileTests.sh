#!/bin/sh
gcc -Wall -I../include test_framing.c ../src/NSC.c -o test_framing && ./test_framing
