#!/bin/bash

gcc server.c NSC.c -o server -lpthread
gcc client.c NSC.c -o client -lpthread

