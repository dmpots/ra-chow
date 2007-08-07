#ifndef __GUARD_LAZY_SET_H
#define __GUARD_LAZY_SET_H

#include <iterator>
#include <Shared.h>
#include "live_range.h"

class LazySet {
  public:
  typedef std::list<LiveRange*> ElemList;
  typedef std::vector<bool> ElemSet;

  /* fields */
  int real_size; /* number of elements in the set */
  bool out_of_sync; /* true if some element was deleted */
  ElemSet* elemset;  /* quick reference for elements in the set */
  ElemList* elemlist; /* may contain removed elements */

  /* constructor */
  LazySet(Arena, int max_size);

  /* methods */
  void insert(LiveRange* lr);
  void erase(LiveRange* lr);
  void clear();
  bool member(LiveRange* lr);
  int size();


  class LazySetIterator : 
    public std::iterator<std::input_iterator_tag, LiveRange*>
  {
    ElemList::iterator it;
    ElemList::iterator end; //end of the iteration range
    ElemSet* real_elems;
    ElemList* elems; 
    bool* out_of_sync; //reset this to false on complete iteration

    public:
    LazySetIterator(
      ElemList::iterator start,
      ElemList::iterator _end,
      ElemSet*  _guards, 
      ElemList* _elems, 
      bool* oos
    )
      : it(start),
        end(_end),
        real_elems(_guards), 
        elems(_elems),
        out_of_sync(oos) { }

    //copy constructor
    LazySetIterator(const LazySetIterator& other)
      : it(other.it),
        end(other.end),
        real_elems(other.real_elems),
        elems(other.elems),
        out_of_sync(other.out_of_sync)
    {
    }

    ElemList::value_type& operator*();
    bool operator==(const LazySetIterator& other) const;
    bool operator!=(const LazySetIterator& other) const;

    //prefix ++
    LazySetIterator& operator++();
    //postfix ++
    LazySetIterator operator++(int);
  };

  typedef LazySetIterator iterator; 
  iterator begin();
  iterator end();

};
#endif
