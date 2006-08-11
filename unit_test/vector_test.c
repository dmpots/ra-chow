#include <Shared.h>
#include <assert.h>
#include <stdio.h>



int main(int argc, char** argv)
{
  unsigned int mRegisters = 3;
  Arena arena = Arena_Create();

  VectorSet all_regs  = VectorSet_Create(arena, mRegisters);
  VectorSet no_regs  = VectorSet_Create(arena, mRegisters);
  VectorSet_Clear(all_regs);
  VectorSet_Clear(no_regs);
  VectorSet_Complement(all_regs, all_regs);

  assert(VectorSet_Size(all_regs) == mRegisters);
  assert(VectorSet_Size(no_regs) == 0);

  VectorSet test = VectorSet_Create(arena, mRegisters);
  VectorSet_Clear(test);
  assert(VectorSet_Equal(test, no_regs));

  unsigned int i;
  for(i = 0; i < mRegisters; i++)
    VectorSet_Insert(test, i);
  assert(VectorSet_Equal(test, all_regs));

  for(i = 0; i < mRegisters; i++)
    VectorSet_Delete(test, i);
  assert(VectorSet_Equal(test, no_regs));


  printf("ALL TEST PASSED!\n");
  return 0;
}
