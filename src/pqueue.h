#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stddef.h>
#include <stdint.h>

typedef void * (* pq_alloc_fn)(size_t size);
typedef void (* pq_free_fn)(void * ptr);
typedef void * (* pq_realloc_fn)(void * ptr, size_t size);
typedef void (* pq_expire_cb)(void * value);

/* priority queue (2-ary heap) */
struct pq;

struct pq * pq_create_custom(int size, pq_expire_cb expire_fn,
        pq_alloc_fn _alloc, pq_free_fn _free, pq_realloc_fn _realloc);
void pq_destroy(struct pq * q);

#define pq_create(size, expire_fn) \
        pq_create_custom(size, expire_fn, NULL, NULL, NULL)

int pq_insert(struct pq * q, uint64_t now, void * value, uint64_t ttl);
/* expire functions return a negative value on error,
 * and the number of expired entries on success */
int pq_expire(struct pq * q, uint64_t now, int num);
int pq_expire_all(struct pq * q, uint64_t now);

void pq_dump_stats(struct pq * q);

/*
 * Advanced API that exposes pq_item to allow rescheduling
 * Memory needs to be handled with the same allocators, and inserting
 * an object gives ownership to the priority queue.
 */
struct pq_item;

struct pq_item * pq_item_create(struct pq * q, uint64_t expire, void * value);
void pq_item_destroy(struct pq * q, struct pq_item * item);

int pq_item_insert(struct pq * q, struct pq_item * item);
int pq_item_remove(struct pq * q, struct pq_item * item);
int pq_item_resched(struct pq * q, uint64_t now, struct pq_item * item,
        uint64_t new_ttl);
void * pq_item_value(struct pq_item * item);

#endif /* PRIORITY_QUEUE_H */
