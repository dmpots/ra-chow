
#include "lazy_set.h"
#include "types.h"
#include "debug.h"

namespace {
  inline bool mem(LazySet::ElemSet* elemset, uint i){
    assert(i < elemset->size());
    return ((*elemset)[i]);
  }
  inline void update(LazySet::ElemSet* elemset, uint i, bool b){
    assert(i < elemset->size());
    ((*elemset)[i]) = b;
  }
}

LazySet::LazySet(Arena arena, int initial_size)
  : real_size(0), out_of_sync(false), seq_id(0)
{
  elemset   = new std::vector<bool>(initial_size, false);
  elemlist  = new ElemList;
}

void LazySet::insert(LiveRange* lr)
{
  if(lr->id >= elemset->size()){
      elemset->resize(LiveRange::counter+100,false);
  };
  if(!mem(elemset,lr->id))
  {
    update(elemset,lr->id, true);
    elemlist->push_back(lr);
    real_size++; assert(real_size <= (int)elemset->size());
    assert(((int)elemlist->size() == real_size) || out_of_sync);
    seq_id++;
  }
}

void LazySet::erase(LiveRange* lr)
{
  if(mem(elemset,lr->id))
  {
    update(elemset,lr->id, false);
    real_size--; assert(real_size >= 0);
    out_of_sync = true;
    seq_id++;
  }
}
bool LazySet::member(LiveRange* lr)
{
  return lr->id < elemset->size() ? mem(elemset,lr->id) : false;
}

void LazySet::clear()
{
  for(uint i = 0; i < elemset->size(); i++){update(elemset,i,false);}
  elemlist->clear();
  real_size = 0;
  out_of_sync = false;
  seq_id++;
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
      if(mem(elemset,(*it)->id)){start = it; break;}
      else{LRList::iterator del = it++; elemlist->erase(del);}
    }
  }

  return LazySetIterator(
    start, elemlist->end(), elemset, elemlist, &out_of_sync,
    real_size, seq_id, &seq_id
  );
}

LazySet::iterator LazySet::end()
{
  return LazySetIterator(
    elemlist->end(), elemlist->end(), NULL, NULL, NULL, -1, -1, NULL
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
        //if we reach then end and the seq_id of the set has not
        //changed then we can reset out_of_sync because we have
        //deleted any non-members from the elems list and the set has
        //not been modifed during this iteration so the two structures
        //should now be in sync
        if(seq_start == *seq_current){
          assert(real_size == (int)elems->size());
          *out_of_sync = false;
          debug("resetting out_of_sync");
        }
        else{debug("out_of_sync not reset, because seq_id changed");}
        break;
      }
      if(!mem(real_elems,(*it)->id))
      {
        ElemList::iterator del = it++;
        elems->erase(del);
      }
      else{found_next = true;}
    }while(!found_next);
  }
  else{debug("not out of sync");}

  return *this;
}

//postfix ++
LazySet::LazySetIterator LazySet::LazySetIterator::operator++(int)
{
  LazySetIterator tmp = *this;
  ++(*this);
  return tmp;
}
