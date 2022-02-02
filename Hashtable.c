/*
htop - Hashtable.c
(C) 2004-2011 Hisham H. Muhammad
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "Hashtable.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "CRT.h"
#include "Macros.h"
#include "XUtils.h"

#ifndef NDEBUG
#include <stdio.h>
#endif

// WARN WARN WARN WARN WARN WARN WARN WARN WARN WARN WARN
// static FILE *_log_file = NULL;

// ATTR_FORMAT(printf, 1, 2)
// static void log_debug(const char *fmt, ...) {
//    if (!_log_file) {
//       char *buf = xMalloc(256);
//       snprintf(buf, 256, "/tmp/htop.%d.log", getpid());
//       _log_file = fopen(buf, "w+");
//       if (!_log_file) {
//          CRT_fatalError("Failed to create log file");
//       }
//    }
//    va_list args;
//    va_start(args, fmt);
//    vfprintf(_log_file, fmt, args);
//    fputc('\n', _log_file);
//    va_end(args);
//    fflush(_log_file);
// }
// WARN WARN WARN WARN WARN WARN WARN WARN WARN WARN WARN

typedef struct HashtableItem_ {
   ht_key_t key;
   size_t probe;
   void* value;
} HashtableItem;

struct Hashtable_ {
   size_t size;
   HashtableItem* buckets;
   size_t items;
   bool owner;
};


#ifndef NDEBUG

static void Hashtable_dump(const Hashtable* this) {
   fprintf(stderr, "Hashtable %p: size=%zu items=%zu owner=%s\n",
           (const void*)this,
           this->size,
           this->items,
           this->owner ? "yes" : "no");

   size_t items = 0;
   for (size_t i = 0; i < this->size; i++) {
      fprintf(stderr, "  item %5zu: key = %5u probe = %2zu value = %p\n",
              i,
              this->buckets[i].key,
              this->buckets[i].probe,
              this->buckets[i].value ? (const void*)this->buckets[i].value : "(nil)");

      if (this->buckets[i].value)
         items++;
   }

   fprintf(stderr, "Hashtable %p: items=%zu counted=%zu\n",
           (const void*)this,
           this->items,
           items);
}

static bool Hashtable_isConsistent(const Hashtable* this) {
   size_t items = 0;
   for (size_t i = 0; i < this->size; i++) {
      if (this->buckets[i].value)
         items++;
   }
   bool res = items == this->items;
   if (!res)
      Hashtable_dump(this);
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

/* https://oeis.org/A014234 */

/*
 * The following values are problematic because they are 2x greater
 * than the next smallest prime:
 *
 * 31 (l:13) 15 true
 * 127 (l:61) 63 true
 * 509 (l:251) 254 true
 * 1021 (l:509) 510 true
 * 4093 (l:2039) 2046 true
 * 8191 (l:4093) 4095 true
 * 65521 (l:32749) 32760 true
 * 131071 (l:65521) 65535 true
 * 524287 (l:262139) 262143 true
 * 4194301 (l:2097143) 2097150 true
 * 16777213 (l:8388593) 8388606 true
 * 67108859 (l:33554393) 33554429 true
 * 268435399 (l:134217689) 134217699 true
 * 536870909 (l:268435399) 268435454 true
 * 2147483647 (l:1073741789) 1073741823 true
 * 34359738337 (l:17179869143) 17179869168 true
 * 68719476731 (l:34359738337) 34359738365 true
*/
static const uint64_t OEISprimes[] = {
   2, 3, 7, 13, 31, 61, 127, 251, 509, 1021, 2039, 4093, 8191,
   16381, 32749, 65521, 131071, 262139, 524287, 1048573,
   2097143, 4194301, 8388593, 16777213, 33554393,
   67108859, 134217689, 268435399, 536870909, 1073741789,
   2147483647UL, 4294967291UL,
#if SIZE_MAX > 4294967295UL
   8589934583UL, 17179869143UL, 34359738337UL, 68719476731UL,
   137438953447UL,
#endif
   SIZE_MAX // sentinel
};

static const size_t primes[] = {
   11, 13, 17, 19, 23, 29, 37, 41, 47, 59, 67, 83, 97, 127, 139, 167, 199, 239,
   293, 347, 419, 499, 593, 709, 853, 1021, 1229, 1471, 1777, 2129, 2543, 3049,
   3659, 4391, 5273, 6323, 7589, 9103, 10937, 13109, 15727, 18899, 22651,
   27179, 32609, 39133, 46957, 56359, 67619, 81157, 97369, 116849, 140221,
   168253, 201907, 242309, 290761, 348889, 418667, 502409, 602887, 723467,
   868151, 1041779, 1250141, 1500181, 1800191, 2160233, 2592277, 3110741,
   3732887, 4479463, 5375371, 6450413, 7740517, 9288589, 11146307, 13375573,
   16050689, 19260817, 23112977, 27735583, 33282701, 39939233, 47927081,
   57512503, 69014987, 82818011, 99381577, 119257891, 143109469, 171731387,
   206077643, 247293161, 296751781, 356102141, 427322587, 512787097, 615344489,
   738413383, 886096061, 1063315271, 1275978331, 1531174013, 1837408799,
   2204890543UL, 2645868653UL, 3175042391UL, 3810050851UL,
#if SIZE_MAX > 4294967295UL
   4572061027UL, 5486473229UL, 6583767889UL, 7900521449UL, 9480625733UL,
   11376750877UL, 13652101063UL, 16382521261UL, 19659025513UL, 23590830631UL,
   28308996763UL, 33970796089UL, 40764955463UL, 48917946377UL, 58701535657UL,
   70441842749UL, 84530211301UL, 101436253561UL, 121723504277UL,
   146068205131UL, 175281846149UL, 210338215379UL, 252405858521UL,
   302887030151UL, 363464436191UL, 436157323417UL, 523388788231UL,
   628066545713UL, 753679854847UL, 904415825857UL, 1085298991109UL,
   1302358789181UL, 1562830547009UL, 1875396656429UL, 2250475987709UL,
   2700571185239UL, 3240685422287UL, 3888822506759UL, 4666587008147UL,
   5599904409713UL, 6719885291641UL, 8063862349969UL, 9676634819959UL,
   11611961783951UL, 13934354140769UL, 16721224968907UL, 20065469962669UL,
   24078563955191UL, 28894276746229UL, 34673132095507UL, 41607758514593UL,
   49929310217531UL, 59915172260971UL, 71898206713183UL, 86277848055823UL,
   103533417666967UL, 124240101200359UL, 149088121440451UL, 178905745728529UL,
   214686894874223UL, 257624273849081UL, 309149128618903UL, 370978954342639UL,
   445174745211143UL, 534209694253381UL, 641051633104063UL, 769261959724877UL,
   923114351670013UL, 1107737222003791UL, 1329284666404567UL,
   1595141599685509UL, 1914169919622551UL, 2297003903547091UL,
   2756404684256459UL, 3307685621107757UL, 3969222745329323UL,
   4763067294395177UL, 5715680753274209UL, 6858816903929113UL,
   8230580284714831UL, 9876696341657791UL, 11852035609989371UL,
   14222442731987227UL, 17066931278384657UL, 20480317534061597UL,
   24576381040873903UL, 29491657249048679UL, 35389988698858471UL,
   42467986438630267UL, 50961583726356109UL, 61153900471627387UL,
   73384680565952851UL, 88061616679143347UL, 105673940014972061UL,
   126808728017966413UL, 152170473621559703UL, 182604568345871671UL,
   219125482015045997UL, 262950578418055169UL, 315540694101666193UL,
   378648832921999397UL, 454378599506399233UL, 545254319407679131UL,
   654305183289214771UL, 785166219947057701UL, 942199463936469157UL,
   1130639356723763129UL, 1356767228068515623UL, 1628120673682218619UL,
   1953744808418662409UL, 2344493770102394881UL, 2813392524122873857UL,
   3376071028947448339UL, 4051285234736937517UL, 4861542281684325481UL,
   5833850738021191727UL, 7000620885625427969UL, 8400745062750513217UL,
   10080894075300616261UL, 12097072890360739951UL, 14516487468432885797UL,
   17419784962119465179UL,
#endif
    SIZE_MAX /* sentinel, to ensure the search terminates */
};


static size_t nextPrime(size_t n) {
   for (size_t i = 0; i < ARRAYSIZE(primes); i++) {
      if (n <= primes[i])
         return primes[i];
   }
   // for (size_t i = 0; i < ARRAYSIZE(OEISprimes); i++) {
   //    if (n <= OEISprimes[i])
   //       return OEISprimes[i];
   // }

   CRT_fatalError("Hashtable: no prime found");
}

Hashtable* Hashtable_new(size_t size, bool owner) {
   Hashtable* this;

   this = xMalloc(sizeof(Hashtable));
   this->items = 0;
   this->size = size ? nextPrime(size) : 13;
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

   assert(Hashtable_isConsistent(this));
}

static void insert(Hashtable* this, ht_key_t key, void* value) {
   size_t index = key % this->size;
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

      index = (index + 1) % this->size;
      probe++;

      assert(index != origIndex);
   }
}

void Hashtable_setSize(Hashtable* this, size_t size) {

   assert(Hashtable_isConsistent(this));

   if (size <= this->items)
      return;

   size_t nextSize = nextPrime(size);
   if (nextSize == this->size) {
      fprintf(stderr, "Same: items: %zu estimate: %zu size: %zu next: %zu\n",
         this->items, size, this->size, nextSize);
      return;
   } else {
      fprintf(stderr, "Update: items: %zu estimate: %zu size: %zu next: %zu\n",
         this->items, size, this->size, nextSize);
   }
   size_t oldSize = this->size;
   this->size = nextSize;

   HashtableItem* oldBuckets = this->buckets;
   this->buckets = (HashtableItem*) xCalloc(this->size, sizeof(HashtableItem));
   this->items = 0;

   /* rehash */
   for (size_t i = 0; i < oldSize; i++) {
      if (!oldBuckets[i].value)
         continue;

      insert(this, oldBuckets[i].key, oldBuckets[i].value);
   }

   free(oldBuckets);

   assert(Hashtable_isConsistent(this));
}

void Hashtable_put(Hashtable* this, ht_key_t key, void* value) {

   assert(Hashtable_isConsistent(this));
   assert(this->size > 0);
   assert(value);

   /* grow on load-factor > 0.7 */
   if (10 * this->items > 7 * this->size) {
      if (SIZE_MAX / 2 < this->size)
         CRT_fatalError("Hashtable: size overflow");

      Hashtable_setSize(this, 2 * this->size);
   }

   insert(this, key, value);

   assert(Hashtable_isConsistent(this));
   assert(Hashtable_get(this, key) != NULL);
   assert(this->size > this->items);
}

void* Hashtable_remove(Hashtable* this, ht_key_t key) {
   size_t index = key % this->size;
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

         size_t next = (index + 1) % this->size;

         while (this->buckets[next].value && this->buckets[next].probe > 0) {
            this->buckets[index] = this->buckets[next];
            this->buckets[index].probe -= 1;

            index = next;
            next = (index + 1) % this->size;
         }

         /* set empty after backward shifting */
         this->buckets[index].value = NULL;
         this->items--;

         break;
      }

      if (this->buckets[index].probe < probe)
         break;

      index = (index + 1) % this->size;
      probe++;

      assert(index != origIndex);
   }

   assert(Hashtable_isConsistent(this));
   assert(Hashtable_get(this, key) == NULL);

   /* shrink on load-factor < 0.125 */
   if (8 * this->items < this->size)
      Hashtable_setSize(this, this->size / 2);

   return res;
}

void* Hashtable_get(Hashtable* this, ht_key_t key) {
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

      index = (index + 1) != this->size ? (index + 1) : 0;
      probe++;

      assert(index != origIndex);
   }

   return res;
}

void Hashtable_foreach(Hashtable* this, Hashtable_PairFunction f, void* userData) {
   assert(Hashtable_isConsistent(this));
   for (size_t i = 0; i < this->size; i++) {
      HashtableItem* walk = &this->buckets[i];
      if (walk->value)
         f(walk->key, walk->value, userData);
   }
   assert(Hashtable_isConsistent(this));
}
