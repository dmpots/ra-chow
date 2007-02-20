/*====================================================================
 * for printing debug info
 *====================================================================
 * $Id: debug.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/debug.h $
 ********************************************************************/


#ifndef __DEBUG_H
#define __DEBUG_H
#include<vector>

#ifdef __DEBUG
#define debug(...) \
                 {fprintf(stderr,"DEBUG [%s,%d] ",__FILE__, __LINE__);\
                fprintf (stderr , __VA_ARGS__);\
                 fprintf(stderr, "\n");}
#else
#define debug(...)
#endif

#define error(...)  \
                 {fprintf(stderr,"ERROR [%s,%d] ",__FILE__, __LINE__);\
                fprintf (stderr , __VA_ARGS__);\
                 fprintf(stderr, "\n");}

#define id(b) ((b)->preorder_index)
#define bname(b) (Label_Get_String((b)->labels->label))
#define oname(op) ((opcode_specs[(op)->opcode]).opcode)

extern std::vector<unsigned int> DEBUG_WatchLRIDs;

#endif /* __DEBUG_H */
