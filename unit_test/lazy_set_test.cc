
#include "../lazy_set.h"
#include "../chow.h"
#include "../live_range.h"
#include "../Shared.h"

int main()
{
  Arena arena = Arena_Create();
  Chow::arena = arena;
  LiveRange::arena = arena;
  int reserved[] = {2,4};
  RegisterClass::Init( arena, 32, true, reserved);

  RegisterClass::RC rc = RegisterClass::RC(0);
  LiveRange* lr1 = new LiveRange(rc,1,0);
  LiveRange* lr2 = new LiveRange(rc,2,0);
  LiveRange* lr3 = new LiveRange(rc,3,0);
  LiveRange* lr4 = new LiveRange(rc,4,0);
  LiveRange* lr5 = new LiveRange(rc,5,0);


  LazySet* lz = new LazySet(arena, 100);

  printf("************* insert test ****************\n");
  lz->insert(lr1);
  lz->insert(lr2);
  assert(lz->size() == 2);

  uint id = 1;
  for(LazySet::iterator it = lz->begin(); it != lz->end(); it++)
  {
    printf("id: %d, itid: %d\n", id, (*it)->id);
    assert(id++ == (*it)->id);
  }

  printf("************* remove all test ****************\n");
  lz->erase(lr1);
  lz->erase(lr2);
  assert(lz->out_of_sync);
  assert(lz->size() == 0);
  id = 0;
  for(LazySet::iterator it = lz->begin(); it != lz->end(); it++)
  {
    printf("id: %d, itid: %d\n", id, (*it)->id);
    assert(false);
  }
  assert(lz->elemlist->empty());
  assert(!lz->out_of_sync);
  for(LazySet::iterator it = lz->begin(); it != lz->end(); it++)
  {
    printf("id: %d, itid: %d\n", id, (*it)->id);
    assert(false);
  }
  printf("************* remove some test ****************\n");
  lz->insert(lr1);
  lz->insert(lr2);
  lz->insert(lr3);
  lz->insert(lr4);
  lz->insert(lr5);
  assert(lz->size() == 5);
  assert(lz->elemlist->size() == 5);
  lz->erase(lr2);
  lz->erase(lr4);
  assert(lz->size() == 3);
  assert(lz->elemlist->size() == 5);
  id = 1;
  for(LazySet::iterator it = lz->begin(); it != lz->end(); it++)
  {
    printf("id: %d, itid: %d\n", id, (*it)->id);
    assert(id == (*it)->id);
    id += 2;
  }
  assert(lz->elemlist->size() == 3);
  lz->erase(lr5);
  lz->insert(lr2);
  assert(lz->size() == 3);
  assert(lz->elemlist->size() == 4);
  int ids[] = {1,3,2};
  id = 0;
  for(LazySet::iterator it = lz->begin(); it != lz->end(); it++)
  {
    printf("id: %d, itid: %d\n", ids[id], (*it)->id);
    assert(ids[id++] == (*it)->id);
  }
  assert(lz->size() == 3);
  assert(lz->elemlist->size() == 3);
 
  printf("************* clear test ****************\n");
  lz->clear();
  assert(lz->size() == 0);
  assert(lz->elemlist->size() == 0);
  assert(!lz->out_of_sync);
  lz->insert(lr1);
  lz->insert(lr2);
  assert(lz->size() == 2);
  lz->clear();
  assert(lz->size() == 0);
  assert(lz->elemlist->size() == 0);
  assert(!lz->out_of_sync);

  printf("************* insert dup test ****************\n");
  lz->clear();
  lz->insert(lr1);
  lz->insert(lr2);
  lz->insert(lr2);
  lz->insert(lr2);
  lz->insert(lr2);
  lz->insert(lr1);
  lz->insert(lr1);
  assert(lz->size() == 2);
  lz->insert(lr3);
  assert(lz->size() == 3);
  int ids2[] = {1,2,3};
  id = 0;
  for(LazySet::iterator it = lz->begin(); it != lz->end(); it++)
  {
    printf("id: %d, itid: %d\n", ids[id], (*it)->id);
    assert(ids2[id++] == (*it)->id);
  }
 
  printf("************* stl tests  ****************\n");
  lz->clear();
  lz->insert(lr1);
  lz->insert(lr2);
  lz->insert(lr5);
  LRVec worklist(lz->size());
  copy(lz->begin(), lz->end(), worklist.begin());
  int ids3[] = {1,2,5};
  id = 0;
  for(LRVec::iterator it = worklist.begin(); it != worklist.end();it++)
  {
    printf("id: %d, itid: %d\n", ids[id], (*it)->id);
    assert(ids3[id++] == (*it)->id);
  }

  printf("ALL TESTS PASSED\n");
}


