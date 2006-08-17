#!/bin/env bash

usage(){
  echo "usage: st [chow|ra] [fmm|spec]"
  exit
}


#register allocators
CHOW_ARGS=""
CHOW=`pwd`/chow $CHOW_ARGS
RA=`pwd`/ra

#test directories
FMM="/home/compiler/test/fmm"
SPEC="/home/compiler/test/spec"

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
PASSES="$DEAD $VALNUM $LAZY $CPROP $COALESCE"

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
ctest $TESTDIR -I -i $PASSES -"{$REGALLOC}" \
 | grep 'Transformations:' >> $STATOUT 

#---- gather stats ----#
$STAT >> $STATOUT
popd > /dev/null
