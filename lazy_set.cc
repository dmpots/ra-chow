
#include "lazy_set.h"
#include "types.h"
#include "debug.h"


LazySet::LazySet(Arena arena, int max_size)
  : real_size(0), out_of_sync(false)
{
  elemset = VectorSet_Create(arena, max_size);
  elemlist = new ElemList;
}

void LazySet::insert(LiveRange* lr)
{
  debug("checking for insert for lr: %d", lr->id);
  if(!VectorSet_Member(elemset, lr->id))
  {
    debug("inserting member");
    VectorSet_Insert(elemset, lr->id);
    elemlist->push_back(lr);
    real_size++;
    debug("new size: %d", real_size);
  }
}

void LazySet::erase(LiveRange* lr)
{
  if(VectorSet_Member(elemset, lr->id))
  {
    VectorSet_Delete(elemset, lr->id);
    real_size--;
    out_of_sync = true;
  }
}

void LazySet::clear()
{
  VectorSet_Clear(elemset);
  elemlist->clear();
  real_size = 0;
  out_of_sync = false;
}

int LazySet::size()
{
  debug("size: %d", real_size);
  return real_size;
}

LazySet::iterator LazySet::begin()
{
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
      if(VectorSet_Member(elemset, (*it)->id)){start = it; break;}
      else{LRList::iterator del = it++; elemlist->erase(del);}
    }
  }

  return LazySetIterator(
    start, elemlist->end(), elemset, elemlist, &out_of_sync
  );
}

LazySet::iterator LazySet::end()
{
  return LazySetIterator(
    elemlist->end(), elemlist->end(), NULL, NULL, NULL
  );
}



LazySet::ElemList::value_type& LazySet::LazySetIterator::operator*()
{
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
  it++;
  if(*out_of_sync)
  {
    bool found_next = false;
    do {
      if(it == end)
      {
        //we reached the end of the sequence and pruned any
        //non-members from the list
        *out_of_sync = false; //reached i
        break;
      }
      if(!VectorSet_Member(real_elems, (*it)->id))
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
