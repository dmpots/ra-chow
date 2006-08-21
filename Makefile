# Chow Allocator Makefile
#

#my macros
CC=g++
SRCFILES=union_find.c\
         chow.c\
         live_range.c\
         cleave.c\
         depths.c\
         rc.c\

OBJS=${SRCFILES:.c=.o}


#use different load flags depending on which compiler we use
#LDFLAGS = -pg

#
# Define the flags used for cc and gcc.  The selection of which
# compiler to use is made above.  For what we need here, the
# CFLAGS are the same for both cc and gcc.
#
CFLAGS  =    -Wall -O3 
#CFLAGS =    -Wall -03 -D__DEBUG 
#CFLAGS = -g -Wall     -D__DEBUG 


#
# Add the names of any executable that you want to build
# to the list below after "ALL = ".
#
CHOW=chow
DOT_DUMP=dot_dump
CLEAVE=cleave
VECTOR_TEST=vtest
SSA_DUMP=ssa_dump
ALL = $(CHOW) $(DOT_DUMP) $(VECTOR_TEST) $(CLEAVE)


#########################################
# CS BUILD PARAMETERS
#########################################
DDIRCOMP=/home/compiler/installed/shared
LDIRSCOMP=$(DDIRCOMP)/archive
LIBSCOMP=$(LDIRSCOMP)/shared-g.a
IDIRSCOMP=-I$(DDIRCOMP)/include

#########################################
# OWLNET BUILD PARAMETERS
#########################################
O_DDIRCOMP=/home/comp512/iloc
O_LDIRSCOMP=$(O_DDIRCOMP)/include
O_LIBSCOMP=$(O_LDIRSCOMP)/shared-g.a
O_IDIRSCOMP=-I$(O_DDIRCOMP)/include


#########################################
# OWLNET BUILD PARAMETERS
#########################################
L_IDIRSCOMP=-I. #local Shared.h comes from cwd


# for building on CS
INCLUDES = $(L_IDIRSCOMP)
LIBS = $(LIBSCOMP)

# for building on owlnet
#INCLUDES = $(L_IDIRSCOMP)
#LIBS = $(O_LIBSCOMP)

default: $(CHOW) 

#top level target to build all programs
all: $(ALL)


######################################
#
# RULES
######################################

$(CHOW): $(OBJS)
	@ $(CC) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"


$(DOT_DUMP): dot_dump.o
	@ $(CC) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

$(CLEAVE): cleave.o cleave.main.o
	@ $(CC) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

$(VECTOR_TEST): unit_test/vector_test.o
	@ $(CC) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

$(SSA_DUMP): ssa_dump.o
	@ $(CC) -o $@ $(LDFLAGS) $^ $(LIBS)
	@ echo " -- make $@ (Done)"

.c.o: 
	@ $(CC) $(CFLAGS) $(INCLUDES) -o $(<:c=o) -c $< 
	@ echo " -- make $@ (Done)"

clean:
	@ rm -f $(OBJS)
	@ rm -f *.o
	@ rm -f $(ALL)
	@ echo " -- make clean (Done)"

sync:
	@rsync -uv -essh --progress --exclude-from=.rsync-excludes \
	boromir.cs.rice.edu:/home/dmp4866/research/compilers/regalloc/src/* .
	@echo " -- make sync (Done)"

