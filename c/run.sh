#!/bin/bash
#make -j2 debug && ./clox testscripts/testfunc.clox
make -j2 debug && ./clox testscripts/testgc.clox
