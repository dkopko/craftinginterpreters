#!/bin/bash

make -j2 debug || exit $?

for i in testscripts/ch25_ex*.clox
do
  echo $i =================================
  setarch `arch` -R ./clox $i |grep -A1 OP_PRINT
  #cat $i
  grep -i "should print" $i
  echo
done
