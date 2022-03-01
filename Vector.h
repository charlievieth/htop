#ifndef HEADER_Vector
#define HEADER_Vector
/*
htop - Vector.h
(C) 2004-2011 Hisham H. Muhammad
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "Object.h"

#include <stdbool.h>


#ifndef DEFAULT_SIZE
#define DEFAULT_SIZE (-1)
#endif

typedef struct Vector_ {
   Object** array;
   const ObjectClass* type;
   int arraySize;
   int growthRate;
   int items;
   /* lowest index of a pending soft remove/delete operation,
      used to speed up compaction */
   int dirty_index;
   /* count of soft deletes, required for Vector_count to work in debug mode */
   int dirty_count;
   bool owner;
} Vector;

Vector* Vector_new(const ObjectClass* type, bool owner, int size);

void Vector_delete(Vector* this);

void Vector_prune(Vector* this);

void Vector_quickSortCustomCompare(Vector* this, Object_Compare compare);
static inline void Vector_quickSort(Vector* this) {
   Vector_quickSortCustomCompare(this, this->type->compare);
}

void Vector_insertionSort(Vector* this);

void Vector_insert(Vector* this, int idx, void* data_);

Object* Vector_take(Vector* this, int idx);

Object* Vector_remove(Vector* this, int idx);

/* Vector_softRemove marks the item at index idx for deletion without
   reclaiming any space. If owned, the item is immediately freed.

   Vector_compact must be called to reclaim space.*/
Object* Vector_softRemove(Vector* this, int idx);

/* Vector_compact reclaims space free'd up by Vector_softRemove, if any. */
void Vector_compact(Vector* this);

void Vector_moveUp(Vector* this, int idx);

void Vector_moveDown(Vector* this, int idx);

void Vector_set(Vector* this, int idx, void* data_);

      // const *Vector v = (const *Vector)(_vec);

#ifndef NDEBUG
#define Vector_assertCount(_vec, _n)                            \
   do {                                                         \
      int count = 0;                                            \
      const int items = (_vec)->items;                          \
      for (int i = 0; i < items; i++) {                         \
         if ((_vec)->array[i]) {                                \
            count++;                                            \
         }                                                      \
      }                                                         \
      assert((size_t)count == (size_t)_n);                      \
   } while (0)
#else
#define Vector_assertCount(_vec, _n) ((void)0)
#endif /* NDEBUG */

#ifndef NDEBUG

Object* Vector_get(const Vector* this, int idx);
int Vector_size(const Vector* this);
unsigned int Vector_count(const Vector* this);

#else /* NDEBUG */


static inline Object* Vector_get(const Vector* this, int idx) {
   return this->array[idx];
}

static inline int Vector_size(const Vector* this) {
   return this->items;
}

#endif /* NDEBUG */

static inline const ObjectClass* Vector_type(const Vector* this) {
   return this->type;
}

void Vector_add(Vector* this, void* data_);

int Vector_indexOf(const Vector* this, const void* search_, Object_Compare compare);

void Vector_splice(Vector* this, Vector* from);

// #define Vector_forEach(vec, idx)
//    for (idx = 0; idx < vec->items; idx++)

#define CONCATX(x, y)      x##y
#define CONCAT(x, y)       CONCATX(x, y)
#define UNIQUE_ALIAS(name) CONCAT(name, __COUNTER__)

/*
 * for_each_if - helper for handling conditionals in various for_each macros
 * @condition: The condition to check
 *
 * Typical use::
 *
 * #define for_each_foo_bar(x, y) \'
 *    list_for_each_entry(x, y->list, head) \'
 *       for_each_if(x->something == SOMETHING)
 *
 * The for_each_if() macro makes the use of for_each_foo_bar() less error
 * prone.
 */
#define for_each_if(condition) if (!(condition)) {} else

/*
 * _Vector_forEach - internal macro to iterate over all entries in Vector.
 *
 * @vec: vector to iterate
 * @value: *Object iteration cursor
 * @__items: int iteration limit, for macro-internal use
 * @__i: int iteration cursor, for macro-internal use
 */
#define _Vector_forEach(vec, value, __items, __i)            \
   const int (__items) = Vector_size(vec);                   \
   for (int (__i) = 0; (__i) < (__items); (__i)++)           \
      for_each_if(((value) = Vector_get((vec), (__i))))

/*
 * Vector_forEach - iterate over all entries in Vector
 * @vec: vector to iterate
 * @value: *Object iteration cursor
 */
#define Vector_forEach(vec, value)                           \
   _Vector_forEach(vec, value, UNIQUE_ALIAS(__items), UNIQUE_ALIAS(__i))

// #define Vector_forEach(vec, value)
//    for (int CONCAT(__i, __LINE__) = 0,
//         CONCAT(__items, __LINE__) = Vector_size(vec);
//         CONCAT(__i, __LINE__) < CONCAT(__items, __LINE__);
//         CONCAT(__i, __LINE__)++)
//       for_each_if(((value) = Vector_get((vec), CONCAT(__i, __LINE__))))


/*
 * _Vector_forEachIndex - internal macro to iterate over all entries in Vector
 * @vec: vector to iterate
 * @__items: int iteration limit, for macro-internal use
 * @__i: int iteration cursor, for macro-internal use
 */
#define _Vector_forEachIndex(vec, __items, __i)              \
   const int (__items) = Vector_size(vec);                   \
   for (int (__i) = 0; (__i) < (__items); (__i)++)           \
      for_each_if(((idx) == __i) || true);

/*
 * Vector_forEachIndex - iterate over all entries in Vector
 * @vec: vector to iterate
 * @idx: int iteration cursor
 */
#define Vector_forEachIndex(vec, idx)                                  \
   for (int __i = 0, __items = Vector_size(vec); __i < __items; __i++) \
      for_each_if(((idx) == __i) || true);

   // for ((idx) = 0; (idx) < ((const Vector*)(vec))->items; (idx)++)

// #define Vector_forEach(vec, idx, val)
//    const int _items = vec->items;
//    for (val = _items > 0 ? vec->array[0] : NULL, idx = 0;
//         idx < _items;
//         idx++, val++)

/*
 * Vector_forEach - iterate over all entries in Vector
 * @vec: vector to iterate
 * @val: object* currect object
 */
#define Vector_forEachValue(vec, val)                           \
   Object* const* _end = &vec->array[vec->items];               \
   Object** _cur = vec->array;                                  \
   for (val = vec->items > 0 ? (__typeof__(val))(*_cur) : NULL; \
        _cur < _end;                                            \
        val = ++_cur != _end ? (__typeof__(val))(*_cur) : NULL)

// #define Vector_forEachValueIdx_XXX(vec, val, idx)
//    for (Object* const* _end = &vec->array[vec->items],
//            Object** _cur = vec->array,
//            val = vec->items > 0 ? (__typeof__(val))(*_cur) : NULL,
//            idx = 0;
//         _cur < _end;
//         val = ++_cur != _end ? (__typeof__(val))(*_cur): NULL,
//            idx++)

/*
 * Vector_forEach - iterate over all entries in Vector
 * @vec: vector to iterate
 * @val: object* currect object
 * @idx: int cursor position
 */
#define Vector_forEachValueIdx(vec, val, idx)                   \
   const int _items = vec->items;                               \
   Object** _cur = vec->array;                                  \
   for (val = vec->items > 0 ? (__typeof__(val))(*_cur) : NULL, \
        idx = 0;                                                \
        idx < _items;                                           \
        val = ++idx != _items ? (__typeof__(val))(*++_cur): NULL)

#endif

// typedef struct Vector_ {
//    void** array;
//    int arraySize;
//    int growthRate;
//    int items;
//    int dirty_index;
//    int dirty_count;
// } Vector;
