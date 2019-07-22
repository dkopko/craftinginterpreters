#!/bin/bash
make -j2 debug && setarch `arch` -R ./clox testscripts/testfunc.clox
