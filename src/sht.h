#ifndef SIMPLE_HASHTABLE_HEADER
#define SIMPLE_HASHTABLE_HEADER

#include <stddef.h>
#include <stdint.h>

typedef void * (* alloc_fn)(size_t size);
typedef void (* free_fn)(void * ptr);
typedef uint32_t (* hash_fn) (void * data, size_t datalen);

/* simple table of linked-lists */
struct sht;

struct sht * sht_create_custom(int size, alloc_fn _alloc, free_fn _free,
        hash_fn _hash);
void sht_destroy(struct sht * h);

#define sht_create(size) sht_create_custom(size, NULL, NULL, NULL)

int sht_insert(struct sht * h, void * key, size_t keylen, void * value);
void * sht_lookup(struct sht * h, void * key, size_t keylen);
void * sht_lookup_insert(struct sht * h, void * key, size_t keylen,
        void * value);
int sht_remove(struct sht * h, void * key, size_t keylen);
int sht_gc(struct sht * h, int max_gc_num);

void sht_dump_stats(struct sht const * h);

#endif /* SIMPLE_HASHTABLE_HEADER */
