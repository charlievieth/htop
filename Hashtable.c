/*
htop - Hashtable.c
(C) 2004-2011 Hisham H. Muhammad
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "Hashtable.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "CRT.h"
#include "Macros.h"
#include "XUtils.h"

#ifndef NDEBUG
#include <stdio.h>
#endif


typedef struct HashtableItem_ {
   ht_key_t key;
   // NOTE: the below is true, but would require 4 billion entries
   // which is: unlikely and would require a truly insane amount of
   // memory. Also, not sure any OS can support 4 billion procs.
   //
   // TODO: this can *maybe* be a uint32_t. The largest key is
   // UINT32_MAX but there is a pathological case where we store
   // UINT32_MAX values and thus the map size is larger than MAX
   // and we end up with a very very long probe.
   size_t probe;
   void* value;
} HashtableItem;

struct Hashtable_ {
   size_t size;
   HashtableItem* buckets;
   size_t items; // TODO: this can be a uint32_t
   uint32_t shrink_requests;
   bool owner;
};


#ifndef NDEBUG
#include <stdarg.h>
#include <stdbool.h>

FILE *hash_debug_file = NULL;

static FILE* init_hash_debug_file() {
   if (hash_debug_file == NULL) {
      FILE *fp = fopen("debug_hash.txt", "a");
      if (!fp) {
         CRT_fatalError("cannot open hash debug file: debug_hash.txt");
         abort();
      }
      fprintf(fp, "\n######################################################\n");
      fprintf(fp, "######################################################\n\n");
      hash_debug_file = fp;
   }
   return hash_debug_file;
}

ATTR_FORMAT(printf, 1, 2)
static void debugf(const char *fmt, ...) {
   va_list args1, args2;
   va_start(args1, fmt);
   va_copy(args2, args1);

   va_list args[2] = {args1, args2};
   FILE *dfp = init_hash_debug_file();
   FILE *fps[2] = {dfp, stderr};

   for (int i = 0; i < 2; i++) {
      FILE *fp = fps[i];
      if (!fp) {
         fprintf(stderr, "WARN: Hashtable: NULL file at index: %d\n", i);
         continue;
      }
      int ret = vfprintf(fp, fmt, args[i]);
      if (ret < 0) {
         fprintf(stderr, "ERROR: debug(\"%s\") returned: %d\n", fmt, ret);
         abort();
      }
   }
   va_end(args1);
   va_end(args2);
   fflush(dfp);
}

static void Hashtable_dump(const Hashtable* this, bool dump_values) {
   size_t max_probe = 0;
   for (size_t i = 0; i < this->size; i++) {
      if (this->buckets[i].value && this->buckets[i].probe > max_probe) {
         max_probe = this->buckets[i].probe;
      }
   }
   // size_t alloc_size = sizeof(HashtableItem) * this->size;
   double mbs = (double)(sizeof(HashtableItem) * this->size) / (double)(1024 * 1024);
   debugf("Hashtable %p: size=%zu items=%zu shrink_requests=%"PRIu32" owner=%s MBs=%.2f max_probe=%zu\n",
           (const void*)this,
           this->size,
           this->items,
           this->shrink_requests,
           this->owner ? "yes" : "no",
           mbs,
           max_probe);

   if (dump_values) {
      size_t items = 0;
      for (size_t i = 0; i < this->size; i++) {
         debugf("  item %5zu: key = %5"PRIu32" probe = %2zu value = %p\n",
                 i,
                 this->buckets[i].key,
                 this->buckets[i].probe,
                 (const void*)this->buckets[i].value);

         if (this->buckets[i].value)
            items++;
      }

      debugf("Hashtable %p: items=%zu counted=%zu\n",
              (const void*)this,
              this->items,
              items);
   }
}

static bool Hashtable_isConsistent(const Hashtable* this) {
   size_t items = 0;
   for (size_t i = 0; i < this->size; i++) {
      if (this->buckets[i].value)
         items++;
   }
   bool res = items == this->items;
   if (!res)
      Hashtable_dump(this, true);
   return res;
}

size_t Hashtable_count(const Hashtable* this) {
   size_t items = 0;
   for (size_t i = 0; i < this->size; i++) {
      if (this->buckets[i].value)
         items++;
   }
   assert(items == this->items);
   return items;
}

#endif /* NDEBUG */

#define MIN_TABLE_SIZE 11

/* Primes borrowed from gnulib/lib/gl_anyhash_primes.h.

   Array of primes, approximately in steps of factor 1.2.
   This table was computed by executing the Common Lisp expression
     (dotimes (i 244) (format t "nextprime(~D)~%" (ceiling (expt 1.2d0 i))))
   and feeding the result to PARI/gp. */
static const size_t primes[] = {
   MIN_TABLE_SIZE, 13, 17, 19, 23, 29, 37, 41, 47, 59, 67, 83, 97, 127, 139,
   167, 199, 239, 293, 347, 419, 499, 593, 709, 853, 1021, 1229, 1471, 1777,
   2129, 2543, 3049, 3659, 4391, 5273, 6323, 7589, 9103, 10937, 13109, 15727,
   18899, 22651, 27179, 32609, 39133, 46957, 56359, 67619, 81157, 97369,
   116849, 140221, 168253, 201907, 242309, 290761, 348889, 418667, 502409,
   602887, 723467, 868151, 1041779, 1250141, 1500181, 1800191, 2160233,
   2592277, 3110741, 3732887, 4479463, 5375371, 6450413, 7740517, 9288589,
   11146307, 13375573, 16050689, 19260817, 23112977, 27735583, 33282701,
   39939233, 47927081, 57512503, 69014987, 82818011, 99381577, 119257891,
   143109469, 171731387, 206077643, 247293161, 296751781, 356102141, 427322587,
   512787097, 615344489, 738413383, 886096061, 1063315271, 1275978331,
   1531174013, 1837408799, 2204890543UL, 2645868653UL, 3175042391UL,
   3810050851UL,
   /* on 32-bit make sure we do not return primes not fitting in size_t */
#if SIZE_MAX > 4294967295ULL
   4572061027ULL, 5486473229ULL, 6583767889ULL, 7900521449ULL, 9480625733ULL,
   /* Largest possible size should be 13652101063ULL == GROWTH_RATE((UINT32_MAX/3)*4)
      we include some larger values in case the above math is wrong */
   11376750877ULL, 13652101063ULL, 16382521261ULL, 19659025513ULL, 23590830631ULL,
#endif
};

static size_t nextPrime(size_t n) {
   for (size_t i = 0; i < ARRAYSIZE(primes); i++) {
      if (n < primes[i]) {
         return primes[i];
      }
   }

   CRT_fatalError("Hashtable: no prime found");
}

Hashtable* Hashtable_new(size_t size, bool owner) {
   Hashtable* this;

   this = xMalloc(sizeof(Hashtable));
   this->items = 0;
   this->shrink_requests = 0;
   this->size = nextPrime(size); // min == 11
   this->buckets = (HashtableItem*) xCalloc(this->size, sizeof(HashtableItem));
   this->owner = owner;

   assert(Hashtable_isConsistent(this));
   return this;
}

void Hashtable_delete(Hashtable* this) {
   Hashtable_clear(this);

   free(this->buckets);
   free(this);
}

void Hashtable_clear(Hashtable* this) {
   assert(Hashtable_isConsistent(this));

   if (this->owner)
      for (size_t i = 0; i < this->size; i++)
         free(this->buckets[i].value);

   memset(this->buckets, 0, this->size * sizeof(HashtableItem));
   this->items = 0;
   this->shrink_requests = 0;

   assert(Hashtable_isConsistent(this));
}

static inline size_t inc_index(size_t index, size_t size) {
   return ++index != size ? index : 0;
}

static void insert(Hashtable* this, ht_key_t key, void* value) {
   const size_t size = this->size;
   size_t index = key % size;
   size_t probe = 0;
#ifndef NDEBUG
   size_t origIndex = index;
#endif

   for (;;) {
      if (!this->buckets[index].value) {
         this->items++;
         this->buckets[index].key = key;
         this->buckets[index].probe = probe;
         this->buckets[index].value = value;
         return;
      }

      if (this->buckets[index].key == key) {
         if (this->owner && this->buckets[index].value != value)
            free(this->buckets[index].value);
         this->buckets[index].value = value;
         return;
      }

      /* Robin Hood swap */
      if (probe > this->buckets[index].probe) {
         HashtableItem tmp = this->buckets[index];

         this->buckets[index].key = key;
         this->buckets[index].probe = probe;
         this->buckets[index].value = value;

         key = tmp.key;
         probe = tmp.probe;
         value = tmp.value;
      }

      index = inc_index(index, size);
      probe++;

      assert(index != origIndex);
   }
}

void Hashtable_setSize(Hashtable* this, size_t size) {

   assert(size == 0 || size > this->items);
   assert(Hashtable_isConsistent(this));

   size_t newSize = nextPrime(size);
   // TODO: we can probably remove this check
   if (newSize == this->size)
      return;

   HashtableItem* oldBuckets = this->buckets;
   size_t oldSize = this->size;

   this->size = newSize;
   this->buckets = (HashtableItem*) xCalloc(this->size, sizeof(HashtableItem));
   this->items = 0;
   this->shrink_requests = 0;

   /* rehash */
   for (size_t i = 0; i < oldSize; i++) {
      if (!oldBuckets[i].value)
         continue;

      insert(this, oldBuckets[i].key, oldBuckets[i].value);
   }

   free(oldBuckets);

   assert(Hashtable_isConsistent(this));
}

// TODO: increase to 3/4 capacity `USABLE_FRACTION(n) ((n)-(n)/4)`
// Or use 4/5 capacity: `USABLE_FRACTION(n) (((n) << 2)/5)`
//
//
/* USABLE_FRACTION is the maximum dictionary load.
 * Currently set to 2/3 capacity.
 */
// #define USABLE_FRACTION(n) (((n) << 1)/3)
#define USABLE_FRACTION(n) (((n) << 2)/5)

/* GROWTH_RATE. Growth rate upon hitting maximum load.
 * Currently set to items*3.
 * This means that hashes double in size when growing without deletions,
 * but have more head room when the number of deletions is on a par with the
 * number of insertions.
 */
#define GROWTH_RATE(h) ((h)->items*3)

/* SHRINK_THRESHOLD. To prevent thrashing, we require the size of the hash
 * to be consistently below the target size. The SHRINK_THRESHOLD is the
 * number of times we must observe the hash's GROWTH_RATE being less than
 * the USABLE_FRACTION (shrink_requests).
 *
 * The SHRINK_THRESHOLD is currently 1/2 of the hashes size, which is roughly
 * 75% of its capacity.
 *
 * If the GROWTH_RATE of the hash exceeds the hash's capacity we reset the
 * shrink_requests counter since shrinking is no longer possible.
 */

#define SHRINK_THRESHOLD(n) (((n) << 1)/8)


static inline void Hashtable_insertResize(Hashtable* this) {
   const size_t size = this->size;
   const size_t items = this->items;

#ifndef NDEBUG
   Hashtable_dump(this, false);
#endif

   /* grow the hash table */
   if (items >= USABLE_FRACTION(size)) {
      Hashtable_setSize(this, GROWTH_RATE(this));
      return;
   }

   /* check if we should shrink the hash table */
   if (MIN_TABLE_SIZE < size && items <= SHRINK_THRESHOLD(size)) {
#ifndef NDEBUG
      debugf("Hashtable: shrinking: size: %zu => %zu items: %zu\n",
         size, GROWTH_RATE(this), this->items);
#endif
      Hashtable_setSize(this, GROWTH_RATE(this));
   }
}

void Hashtable_put(Hashtable* this, ht_key_t key, void* value) {

   assert(Hashtable_isConsistent(this));
   assert(this->size > 0);
   assert(value);

   Hashtable_insertResize(this);
   insert(this, key, value);

   assert(Hashtable_isConsistent(this));
   assert(Hashtable_get(this, key) != NULL);
   assert(this->size > this->items);
}

void* Hashtable_remove(Hashtable* this, ht_key_t key) {
   const size_t size = this->size;
   size_t index = key % size;
   size_t probe = 0;
#ifndef NDEBUG
   size_t origIndex = index;
#endif

   assert(Hashtable_isConsistent(this));

   void* res = NULL;

   while (this->buckets[index].value) {
      if (this->buckets[index].key == key) {
         if (this->owner) {
            free(this->buckets[index].value);
         } else {
            res = this->buckets[index].value;
         }

         size_t next = inc_index(index, size);

         while (this->buckets[next].value && this->buckets[next].probe > 0) {
            this->buckets[index] = this->buckets[next];
            this->buckets[index].probe -= 1;

            index = next;
            next = inc_index(index, size);
         }

         /* set empty after backward shifting */
         this->buckets[index].value = NULL;
         this->items--;

         break;
      }

      if (this->buckets[index].probe < probe)
         break;

      index = inc_index(index, size);
      probe++;

      assert(index != origIndex);
   }

   assert(Hashtable_isConsistent(this));
   assert(Hashtable_get(this, key) == NULL);

   return res;
}

void* Hashtable_get(const Hashtable* this, ht_key_t key) {
   size_t index = key % this->size;
   size_t probe = 0;
   void* res = NULL;
#ifndef NDEBUG
   size_t origIndex = index;
#endif

   assert(Hashtable_isConsistent(this));

   while (this->buckets[index].value) {
      if (this->buckets[index].key == key) {
         res = this->buckets[index].value;
         break;
      }

      if (this->buckets[index].probe < probe)
         break;

      index = inc_index(index, this->size);
      probe++;

      assert(index != origIndex);
   }

   return res;
}

// void* Hashtable_get_XXX(const Hashtable* this, ht_key_t key) {
//    size_t index = key % this->size;
//    size_t probe = 0;
//
//    HashtableItem *buckets = this->buckets;
//    const HashtableItem *b = &buckets[index];
//    const HashtableItem *end = &buckets[this->size - 1];
//
//    while (b->value) {
//       if (b->key == key) {
//          return b->value;
//       }
//       if (b->probe < probe) {
//          break;
//       }
//       b = b < end ? b + 1 : buckets;
//       probe++;
//    }
//    return NULL;
// }

void Hashtable_foreach(Hashtable* this, Hashtable_PairFunction f, void* userData) {
   assert(Hashtable_isConsistent(this));
   for (size_t i = 0; i < this->size; i++) {
      HashtableItem* walk = &this->buckets[i];
      if (walk->value)
         f(walk->key, walk->value, userData);
   }
   assert(Hashtable_isConsistent(this));
}
