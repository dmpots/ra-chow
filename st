#!/bin/env bash

usage(){
  echo "usage: st [chow|ra] [fmm|spec|spec95]"
  exit
}


#register allocators
CHOW_ARGS="-r32 -p -b5 -m"
CHOW="`pwd`/chow $CHOW_ARGS"
RA="`pwd`/ra"

#test directories
FMM="/home/compiler/test/fmm"
SPEC="/home/compiler/test/spec"
SPEC95="/home/compiler/test/spec95X"

#process command line params to get register allocator and test dir
case $1 in
  "chow")
    REGALLOC=$CHOW;;
  "ra")
    REGALLOC=$RA;;
  *)
    usage;;
esac
case $2 in
  "fmm")
    TESTDIR=$FMM;;
  "spec")
    TESTDIR=$SPEC;;
  "spec95")
    TESTDIR=$SPEC95;;
  *)
    usage;;
esac
case $3 in
  "")
    echo "running full test suite";;
  *)
    echo "running test $3"
    TESTDIR=$TESTDIR/$3;;
esac


#transformations
DEAD=-d
VALNUM=-v
LAZY=-z
CPROP=-c
COALESCE=-s
CLEAN=-n
GVAL="-g[-p]"
PRE_PASS="$DEAD $VALNUM $GVAL $LAZY $CPROP $COALESCE $DEAD"
POST_PASS="$CLEAN"

#statistics gathering program
TMPDIR=scratch
STATDIR="`pwd`/$TMPDIR"
STATOUT="`pwd`/tmp.stats"
CSVOUT="`pwd`/tmp.stats.csv"
STAT="ruby `pwd`/st.rb $STATDIR $CSVOUT"

#prepare directory
rm -rf $TMPDIR
mkdir  $TMPDIR
pushd  $TMPDIR > /dev/null

#---- run ctest ----#
date > $STATOUT
ctest $TESTDIR -I -i $PRE_PASS -"{$REGALLOC}" $POST_PASS\
 | grep 'Transformations:' >> $STATOUT 

#---- gather stats ----#
cat $STATOUT > $CSVOUT  #get header
$STAT >> $STATOUT
popd > /dev/null

