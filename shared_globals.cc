/* globals.cc
 *
 * contains global variables that must be used to interact with shared
 * iloc code. These extern variables are defined here in the normal c
 * way and also in the Globals namespace, which is how they are
 * accessed in the chow allocator.
 */
#include "shared_globals.h"

unsigned int* depths;
unsigned int* Globals::depths;

