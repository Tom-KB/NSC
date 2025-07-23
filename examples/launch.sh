#!/bin/bash

gcc mainServer.c NSC.c -o testServer -lpthread
gcc mainClient.c NSC.c -o testClient -lpthread

