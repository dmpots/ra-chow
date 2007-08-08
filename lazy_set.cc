
#include "lazy_set.h"
#include "types.h"
#include "debug.h"

namespace {
  inline bool mem(LazySet::ElemSet* elemset, uint i){
    assert(i < elemset->size());
    return ((*elemset)[i]);
  }
  inline void add(LazySet::ElemSet* elemset, uint i, bool b){
    assert(i < elemset->size());
    ((*elemset)[i]) = b;
  }
}

LazySet::LazySet(Arena arena, int max_size)
  : real_size(0), out_of_sync(false)
{
  //elemset = VectorSet_Create(arena, max_size);
  elemset   = new std::vector<bool>(10, false);
  elemlist  = new ElemList;
}

void LazySet::insert(LiveRange* lr)
{
  //debug("checking for insert for lr: %d", lr->id);
  //if(!VectorSet_Member(elemset, lr->id))
  if(lr->id >= elemset->size()){
      elemset->resize(LiveRange::counter+100,false);
  };
  if(!mem(elemset,lr->id))
  {
    //debug("inserting member");
    //VectorSet_Insert(elemset, lr->id);
    add(elemset,lr->id, true);
    elemlist->push_back(lr);
    real_size++; assert(real_size <= (int)elemset->size());
    assert(elemlist->size() == real_size || out_of_sync);
    //debug("new size: %d", real_size);
  }
}

void LazySet::erase(LiveRange* lr)
{
  //if(VectorSet_Member(elemset, lr->id))
  if(mem(elemset,lr->id))
  {
    //VectorSet_Delete(elemset, lr->id);
    add(elemset,lr->id, false);
    real_size--; assert(real_size >= 0);
    out_of_sync = true;
  }
}
bool LazySet::member(LiveRange* lr)
{
  return lr->id < elemset->size() ? mem(elemset,lr->id) : false;
  //return VectorSet_Member(elemset, lr->id);
}

void LazySet::clear()
{
  //VectorSet_Clear(elemset);
  for(uint i = 0; i < elemset->size(); i++){add(elemset,i,false);}
  elemlist->clear();
  real_size = 0;
  out_of_sync = false;
}

int LazySet::size()
{
  assert(real_size < (int)elemset->size());
  assert(real_size >= 0);
  return real_size;
}

LazySet::iterator LazySet::begin()
{
  //printf("looking for begin. size: %d, real_size: %d\n", 
  //  elemset->size(), real_size);
  LRList::iterator start = elemlist->end();
  if(real_size == 0)
  {
    elemlist->clear();
    out_of_sync = false;
  }
  else
  {
    //find the first element in the list that is still in the set and
    //start the iteration from there, erasing expired as we go
    for(LRList::iterator it = elemlist->begin(); it != elemlist->end();)
    {
      //if(VectorSet_Member(elemset, (*it)->id)){start = it; break;}
      debug("mem? %d", (*it)->id);
      if(mem(elemset,(*it)->id)){start = it; break;}
      else{LRList::iterator del = it++; elemlist->erase(del);}
    }
  }

  return LazySetIterator(
    start, elemlist->end(), elemset, elemlist, &out_of_sync, real_size
  );
}

LazySet::iterator LazySet::end()
{
  return LazySetIterator(
    elemlist->end(), elemlist->end(), NULL, NULL, NULL, -1
  );
}



LazySet::ElemList::value_type& LazySet::LazySetIterator::operator*()
{
  assert(it != end);
  return *it;
}

bool LazySet::LazySetIterator::operator==(const LazySetIterator& other)
 const
{
  return it == other.it;
}

bool LazySet::LazySetIterator::operator!=(const LazySetIterator& other) 
 const
{
  return it != other.it;
}

//prefix ++
LazySet::LazySetIterator& LazySet::LazySetIterator::operator++()
{
  assert(it != end);
  it++;
  if(*out_of_sync)
  {
    bool found_next = false;
    do {
      if(it == end)
      {
        //if we end up with our elem list having the same size as our
        //expected size then say we are no longer out of sync. this
        //was biting us due to incorrectly setting out_of_sync to be
        //false when we were modifying the lazy set during iteration.
        //it will still get us if we both add and remove from the set,
        //but that does not happen i believe
        if(real_size == elems->size()) *out_of_sync = false;
        break;
      }
      //if(!VectorSet_Member(real_elems, (*it)->id))
      if(!mem(real_elems,(*it)->id))
      {
        ElemList::iterator del = it++;
        elems->erase(del);
      }
      else{found_next = true;}
    }while(!found_next);
  }

  return *this;
}

//postfix ++
LazySet::LazySetIterator LazySet::LazySetIterator::operator++(int)
{
  LazySetIterator tmp = *this;
  ++(*this);
  return tmp;
}
