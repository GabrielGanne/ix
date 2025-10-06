#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "pqueue.h"

#define DEFAULT_CAPACITY 64

struct pq_item {
    int idx; /* index in heap */
    uint64_t expire;
    void * value;
};

struct pq {
    struct pq_item ** items;
    int size;
    int capacity;

    /* global lock */
    pthread_mutex_t lock;

    pq_expire_cb expire_cb;
    pq_alloc_fn alloc;
    pq_free_fn free;
    pq_realloc_fn realloc;

    /* stats */
    uint64_t cpt_insert;
    uint64_t cpt_expire;
    uint64_t cpt_resched;
    uint64_t cpt_remove;
    uint64_t cpt_double_size;
    uint64_t cpt_double_size_fail;
};

__attribute__((malloc))
__attribute__((alloc_size(1)))
struct pq * pq_create_custom(int size, pq_expire_cb expire_fn, pq_alloc_fn
        _alloc,
        pq_free_fn _free, pq_realloc_fn _realloc)
{
    struct pq * q;
    struct pq_item ** items;

    if (_alloc == NULL)
        _alloc = malloc;

    if (_free == NULL)
        _free = free;

    if (_realloc == NULL)
        _realloc = realloc;

    if (size <= 0)
        size = DEFAULT_CAPACITY;

    items = _alloc(sizeof(*items) * size);
    if (items == NULL)
        return NULL;

    q = _alloc(sizeof(*q));
    if (q == NULL) {
        _free(items);
        return NULL;
    }

    *q = (struct pq) {
        .items = items,
        .size = 0,
        .capacity = size,
        .expire_cb = expire_fn,
        .alloc = _alloc,
        .free = _free,
        .realloc = _realloc,
    };
    pthread_mutex_init(&q->lock, NULL);

    return q;
}

void pq_destroy(struct pq * q)
{
    if (q == NULL)
        return;

    pq_expire_all(q, UINT64_MAX);
    pthread_mutex_destroy(&q->lock);
    q->free(q->items);
    q->free(q);
}

__attribute__((nonnull(1)))
struct pq_item * pq_item_create(struct pq * q, uint64_t expire, void * value)
{
    struct pq_item * item;

    assert(q != NULL);

    item = q->alloc(sizeof(*item));
    if (unlikely(item == NULL))
        return NULL;

    *item = (struct pq_item) {
        .expire = expire,
        .value = value,
    };

    return item;
}

__attribute__((nonnull(1)))
void pq_item_destroy(struct pq * q, struct pq_item * item)
{
    assert(q != NULL);

    if (item != NULL) {
        q->free(item);
    }
}


/* queue is already locked */
__attribute__((nonnull(1)))
static inline
int pq_double_size(struct pq * q)
{
    struct pq_item ** tmp;
    int new_size;

    assert(q != NULL);

    q->cpt_double_size++;

    new_size = q->capacity << 1;
    tmp = q->realloc(q->items, new_size * sizeof(*q->items));
    if (unlikely(tmp == NULL)) {
        q->cpt_double_size_fail++;
        return -1;
    }

    q->items = tmp;
    q->capacity = new_size;

    return 0;
}


__attribute__((nonnull(1, 2)))
static inline
void swap_items(struct pq_item ** x, struct pq_item ** y)
{
    struct pq_item * tmp;

    tmp = *x;
    *x = *y;
    *y = tmp;
}

__attribute__((nonnull(1)))
static inline
void heapify_up(struct pq * q, int idx)
{
    int parent_idx;

    if (idx == 0)
        return;

    assert(q != NULL);

    parent_idx = (idx - 1) / 2;
    if (q->items[idx]->expire < q->items[parent_idx]->expire) {
        swap_items(&q->items[idx], &q->items[parent_idx]);
        heapify_up(q, parent_idx);
    }
}


__attribute__((nonnull(1)))
static inline
void heapify_down(struct pq* q, int idx)
{
    int left_child = 2 * idx + 1;
    int right_child = 2 * idx + 2;
    int smallest = idx;

    if (left_child < q->size
        && q->items[left_child]->expire < q->items[smallest]->expire) {
        smallest = left_child;
    }

    if (right_child < q->size
        && q->items[right_child]->expire < q->items[smallest]->expire) {
        smallest = right_child;
    }

    if (smallest != idx) {
        swap_items(&q->items[idx], &q->items[smallest]);
        heapify_down(q, smallest);
    }
}

__attribute__((nonnull(1, 2)))
int pq_item_insert(struct pq * q, struct pq_item * item)
{
    int rv;

    assert(q != NULL);
    assert(item != NULL);

    pthread_mutex_lock(&q->lock);

    if (q->size == q->capacity) {
        rv = pq_double_size(q);
        if (unlikely(rv != 0))
            return rv;
    }

    item->idx = q->size;
    q->items[q->size] = item;
    q->size++;
    heapify_up(q, q->size - 1);

    q->cpt_insert++;

    pthread_mutex_unlock(&q->lock);

    return 0;
}

__attribute__((nonnull(1, 3)))
int pq_item_resched(struct pq * q, uint64_t now, struct pq_item * item,
        uint64_t new_ttl)
{
    int rv;

    assert(q != NULL);
    assert(item != NULL);

    rv = pq_item_remove(q, item);
    if (unlikely(rv != 0))
        return rv;

    item->expire = now + new_ttl;
    q->cpt_resched++;

    return pq_item_insert(q, item);
}

__attribute__((nonnull(1, 2)))
int pq_item_remove(struct pq * q, struct pq_item * item)
{
    int idx;
    int parent_idx;

    assert(q != NULL);
    assert(item != NULL);

    pthread_mutex_lock(&q->lock);
    idx = item->idx;

    if (idx == q->size - 1) {
        q->size--;
    } else {
        q->items[idx] = q->items[q->size - 1];
        q->size--;

        parent_idx = (idx - 1) / 2;
        if (idx > 0
            && q->items[idx]->expire < q->items[parent_idx]->expire) {
            heapify_up(q, idx);
        } else {
            heapify_down(q, idx);
        }
    }

    q->cpt_remove++;

    pthread_mutex_unlock(&q->lock);

    return 0;
}

__attribute__((nonnull(1)))
int pq_insert(struct pq * q, uint64_t now, void * value, uint64_t ttl)
{
    struct pq_item * item;

    assert(q != NULL);

    item = pq_item_create(q, now + ttl, value);
    if (unlikely(item == NULL))
        return -1;

    return pq_item_insert(q, item);
}

__attribute__((nonnull(1)))
int pq_expire(struct pq * q, uint64_t now, int num)
{
    struct pq_item * item;
    int cpt;

    assert(q != NULL);

    if (num <= 0)
        return 0;

    pthread_mutex_lock(&q->lock);

    cpt = 0;
    while (cpt < num) {
        if (q->size == 0) {
            pthread_mutex_unlock(&q->lock);
            break;
        }

        item = q->items[0];
        if (item->expire > now)
            break;

        if (q->expire_cb != NULL)
            q->expire_cb(item->value);

        q->items[0] = q->items[q->size - 1];
        pq_item_destroy(q, item);
        q->size--;
        heapify_down(q, 0);
    }

    q->cpt_expire += cpt;
    pthread_mutex_unlock(&q->lock);

    return cpt;
}

__attribute__((nonnull(1)))
int pq_expire_all(struct pq * q, uint64_t now)
{
    return pq_expire(q, now, INT32_MAX);
}

__attribute__((nonnull(1)))
void * pq_item_value(struct pq_item * item)
{
    return item->value;
}

__attribute__((nonnull(1)))
void pq_dump_stats(struct pq * q)
{
    printf("inserts: %lu\n", q->cpt_insert);
    printf("expirations: %lu\n", q->cpt_expire);
    printf("rescheduled: %lu\n", q->cpt_resched);
    printf("removes: %lu\n", q->cpt_remove);
    printf("double-size: %lu\n", q->cpt_double_size);
    printf("failed double-size: %lu\n", q->cpt_double_size_fail);
}
