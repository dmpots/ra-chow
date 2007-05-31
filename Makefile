# Chow Allocator Makefile
#

#set c++ compiler
CXX=g++

#
# program sources
#
SRCFILES=union_find.cc\
         live_range.cc\
         chow.cc\
         cleave.cc\
         depths.cc\
         rc.cc\
         assign.cc\
         cfg_tools.cc\
         dot_dump.cc\
         shared_globals.cc\
         params.cc\
         debug.cc\
         live_unit.cc\
         spill.cc\
         color.cc\
         stats.cc\
         mapping.cc\
         chow_extensions.cc\
         rematerialize.cc\
         reach.cc\
         heuristics.cc\

MAIN_SRC=chow.main.cc
#
# automatically infer the object files
#
OBJS=${SRCFILES:.cc=.o}
MAIN_OBJ= ${MAIN_SRC:.cc=.o}

#
# CXXFLAGS will be passed to the c++ compiler
#
##CXXFLAGS  =    -Wall -O3 
##CXXFLAGS =    -Wall -O3 -D__DEBUG 
##CXXFLAGS = -g -Wall     -D__DEBUG 
OPT=-O
CXXFLAGS = -g -Wall $(OPT) $(DEFS)

#
# Flags to pass to the linker/loader
#
##LDFLAGS = -pg

# 
# GENERATED EXECUTABLES
# 
CHOW=chow
DOT_DUMP=dot_dump
CLEAVE=cleave
SPLIT=splitE
VECTOR_TEST=vtest
SSA_DUMP=ssa_dump
ALL = $(CHOW) $(DOT_DUMP) $(VECTOR_TEST) $(CLEAVE) $(SPLIT)


#
# LIBRARIES
#
SHARED_LIB=/home/compiler/installed/shared/archive/shared-g.a
LIBS = $(SHARED_LIB)

#
# INCLUDES
#
#local Shared.h comes from cwd
INCLUDES = -I.
CXXFLAGS += $(INCLUDES)


#
# DEFAULT TARGET
#
default: $(CHOW) 

#top level target to build all programs
.PHONEY: all clean clobber sync benchmark
all: $(ALL)


#==================================
#             RULES
#==================================

#
# include dependency files which will be generated automatically
#
include $(SRCFILES:.cc=.d)
include $(MAIN_SRC:.cc=.d)

#
# Chow Allocator
#
$(CHOW):$(OBJS) $(MAIN_OBJ) 
	@ $(CXX) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

debug: DEFS += -D__DEBUG
debug: OPT =
debug: $(OBJS) $(MAIN_OBJ)
	@ $(CXX) -o $@ $(LDFLAGS) $^ $(LIBS)
	cp debug chow
	@ echo " -- make $@ (Done)"

chow-benchmark: $(CHOW) 
	cp $(CHOW) chow-benchmark

#
# Extra utility programs
#
# set search path to look in some subdirs
vpath %.cc unit_test
vpath %.cc util

$(DOT_DUMP): dot_dump.o dot_dump.main.o $(OBJS)
	@ $(CXX) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

$(CLEAVE): cfg_tools.o cleave.o cleave.main.o
	@ $(CXX) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

$(SPLIT): cfg_tools.o cfg_tools.main.o
	@ $(CXX) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

$(VECTOR_TEST): vector_test.o
	@ $(CXX) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

$(SSA_DUMP): ssa_dump.o
	@ $(CXX) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

#
# Cleanup targets
#
clean:
	@ rm -f *.o
	@ rm -f $(CHOW)
	@ rm -f chow-benchmark
	@ rm -f debug
	@ echo " -- make clean (Done)"

clobber: clean
	@ rm -f $(ALL)
	@ rm -f *.d
	@ echo " -- make clobber (Done)"

#==================================
#            PATTERNS 
#==================================

#
# build .o from .cc files
#
%.o : %.cc
	 $(CXX) $(CXXFLAGS) -o $@ -c $<
##	@ echo " -- make $@ (Done)"

#
# automatically generate dependencies for the .cc files to be included
# in the make file. each .cc file will have a .d file generated
# that contains the dependencies determined by looking at the #include
# directives
#
# this pattern taken from GNU Make book
#
%.d : %.cc
	@ $(CXX) -MM $(CXXFLAGS) $< > $@.$$$$; \
  sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
  rm -f $@.$$$$
	@ echo " -- make dependency file $@ (Done)"


#
# Sync target left for prosperity in case it is useful later
#
##sync:
##	@rsync -uv -essh --progress --exclude-from=.rsync-excludes \
##	boromir.cs.rice.edu:/home/dmp4866/research/compilers/regalloc/src/* .
##	@echo " -- make sync (Done)"

