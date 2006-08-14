#!/bin/env bash

#register allocators
CHOW_ARGS=""
CHOW=`pwd`/chow $CHOW_ARGS
RA=`pwd`/ra

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
STAT="ruby `pwd`/st.rb $STATDIR"

#prepare directory
rm -rf $TMPDIR
mkdir  $TMPDIR
pushd  $TMPDIR > /dev/null

#---- run ctest ----#
date > $STATOUT
#ctest /home/compiler/test/fmm -I -i $PASSES -"{$CHOW}" \
# | grep 'Transformations:' >> $STATOUT
ctest /home/compiler/test/fmm -I -i $PASSES -"{$RA}" \
 | grep 'Transformations:' >> $STATOUT


#---- gather stats ----#
$STAT >> $STATOUT
popd > /dev/null

