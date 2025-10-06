#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "sht.h"

#define DEFAULT_NUM_LINES 100

struct node {
    uint32_t hash;
    void * key;
    size_t keylen;
    void * data;

    struct node * next;
};

struct line {
    pthread_rwlock_t lock;
    int len;
    struct node * nodes;
};

struct sht {
    struct sht * old;
    int do_double_size;
    int gc_index;
    int gc_num;
    int max_line_depth;

    sht_hash_fn hash;
    struct line * lines;
    int size;

    int ref;
    pthread_spinlock_t global_lock;

    sht_alloc_fn alloc;
    sht_free_fn free;

    /* stats */
    uint64_t cpt_lookup;
    uint64_t cpt_insert;
    uint64_t cpt_remove;
    uint64_t cpt_collisions;
    uint64_t cpt_double_size;
    uint64_t cpt_double_size_fail;
};

/* integer square root */
#define ISQRRT_NEXT(n, i)  (((n) + (i) / (n)) >> 1)

static inline uint32_t
isqrt(uint64_t number)
{
    uint64_t n, n1;

    if (unlikely(number == UINT64_MAX))
        return UINT32_MAX;

    n = 1;
    n1 = ISQRRT_NEXT(n, number);

    while (abs((signed)n1 - (signed)n) > 1) {
        n = n1;
        n1 = ISQRRT_NEXT(n, number);
    }

    while (n1 * n1 > number) {
        n1--;
    }

    return (uint32_t) n1;
}

static struct node *
node_create(struct sht * h, void * key, size_t keylen, void * data)
{
    struct node * b;
    void * key_cpy;

    assert(h != NULL);

    if (unlikely(key == NULL || keylen == 0))
        return NULL;

    b = h->alloc(sizeof(*b));
    if (unlikely(b == NULL))
        return NULL;

    key_cpy = h->alloc(keylen);
    if (unlikely(key_cpy == NULL)) {
        h->free(b);
        return NULL;
    }

    memcpy(key_cpy, key, keylen);

    *b = (struct node) {
        .hash = h->hash(key, keylen),
        .key = key_cpy,
        .keylen = keylen,
        .data = data,
    };

    return b;
}

static void
node_destroy(sht_free_fn _free, struct node * b)
{
    if (b != NULL) {
        _free(b->key);
        _free(b);
    }
}

static void
line_init(struct line * line)
{
    memset(line, 0, sizeof(*line));
    pthread_rwlock_init(&line->lock, NULL);
}

static void
line_deinit(sht_free_fn _free, struct line * line)
{
    struct node * b, * tmp;

    assert(line != NULL);

    pthread_rwlock_wrlock(&line->lock);
    b = line->nodes;
    while (b != NULL) {
        tmp = b->next;
        node_destroy(_free, b);
        b = tmp;
    }

    pthread_rwlock_unlock(&line->lock);
    pthread_rwlock_destroy(&line->lock);
}

static void
line_insert(struct line * line, struct node * node)
{
    node->next = line->nodes;
    line->nodes = node;

    atomic_incr(line->len);
}

/* expects line to be locked */
static inline
void * line_lookup(struct line * line, void * key, size_t keylen, uint32_t hash)
{
    struct node * node;

    for (node = line->nodes ; node != NULL ; node = node->next) {
        if (node->hash != hash)
            continue;

        if (likely(node->keylen == keylen
                  && memcmp(node->key, key, keylen) == 0)) {
            return node->data;
        }
    }

    return NULL;
}

void sht_destroy(struct sht * h)
{
    int i;

    if (h != NULL) {
        for (i = 0 ; i < h->size ; i++) {
            line_deinit(h->free, &h->lines[i]);
        }

        pthread_spin_destroy(&h->global_lock);
        h->free(h->lines);
        h->free(h);
    }
}


__attribute__((malloc))
__attribute__((alloc_size(1)))
struct sht * sht_create_custom(int size, sht_alloc_fn _alloc, sht_free_fn _free,
        sht_hash_fn _hash)
{
    int i;
    struct line * lines;
    struct sht * h;

    if (_alloc == NULL)
        _alloc = malloc;

    if (_free == NULL)
        _free = free;

    if (_hash == NULL)
        _hash = oat_hash;

    h = _alloc(sizeof(*h));
    if (h == NULL)
        return NULL;

    if (size <= 0)
        size = DEFAULT_NUM_LINES;

    lines = _alloc(size * sizeof(*lines));
    if (lines == NULL) {
        _free(h);
        return NULL;
    }

    for (i = 0 ; i < size ; i++) {
        line_init(&lines[i]);
    }

    *h = (struct sht) {
        .gc_num = 10,
        .do_double_size = 1,
        .max_line_depth = isqrt(size),
        .hash = _hash,
        .lines = lines,
        .size = size,
        .alloc = _alloc,
        .free = _free,
    };
    pthread_spin_init(&h->global_lock, PTHREAD_PROCESS_PRIVATE);

    return h;
}

/*
 * The global spinlock is here to block new hashtable operation
 * during double-size.
 */
static ALWAYS_INLINE
void sht_ref(struct sht * h)
{
    pthread_spin_lock(&h->global_lock);
    atomic_incr(h->ref);
    pthread_spin_unlock(&h->global_lock);
}

static int
sht_double_size(struct sht * h)
{
    int i, exit;
    struct sht * old;
    int new_size;
    struct line * new_lines;

    /* make sure we don't try to double size
     * twice at the same time */
    exit = 1;
    if (pthread_spin_trylock(&h->global_lock) == 0) {
        if (h->do_double_size) {
            exit = 0;
            h->do_double_size = 0;
        }

        pthread_spin_unlock(&h->global_lock);
    }

    if (exit)
        return 0;

    /* too many double sizes too fast */
    if (unlikely(h->old != NULL))
        return -1;

    /* prepare */
    old = h->alloc(sizeof(*old));
    if (unlikely(old == NULL))
        return -1;

    new_size = h->size * 2;
    new_lines = h->alloc(new_size * sizeof(*new_lines));
    if (unlikely(new_lines == NULL)) {
        h->free(old);
        return -1;
    }

    for (i = 0 ; i < new_size ; i++)
        line_init(&new_lines[i]);

    /* put on old any new hashtable operation */
    pthread_spin_lock(&h->global_lock);

    /* wait for any (other) current hashtable transaction to finish */
    while (atomic_read(h->ref) > 1)
        ;

    *old = *h;
    h->size = new_size;
    h->max_line_depth = isqrt(new_size);
    h->lines = new_lines;
    h->cpt_double_size++;

    pthread_spin_init(&old->global_lock, PTHREAD_PROCESS_PRIVATE);
    h->old = old;
    h->do_double_size = 1;

    /* resume operations */
    pthread_spin_unlock(&h->global_lock);

    return 0;
}

static inline
void sht_insert_node(struct sht * h, struct line * line, struct node * node)
{
    void * bak;

    atomic_incr(h->cpt_insert);

    pthread_rwlock_wrlock(&line->lock);
    bak = line->nodes;
    line_insert(line, node);
    pthread_rwlock_unlock(&line->lock);

    if (bak != NULL)
        atomic_incr(h->cpt_collisions);
}

__attribute__((nonnull(1)))
int sht_insert(struct sht * h, void * key, size_t keylen, void * value)
{
    struct node * node;
    struct line * line;

    if (unlikely(key == NULL || keylen == 0))
        return -1;

    node = node_create(h, key, keylen, value);
    if (unlikely(node == NULL))
        return -1;

    sht_ref(h);
    line = &h->lines[node->hash % h->size];

    if (unlikely(atomic_read(line->len) > h->max_line_depth)) {
        if (likely(sht_double_size(h) == 0))
            line = &h->lines[node->hash % h->size];
    }

    sht_insert_node(h, line, node);
    atomic_decr(h->ref);

    return 0;
}

static
int _sht_gc(struct sht * h, int max_gc_num)
{
    int n, i;
    struct line * old_line, * new_line;
    struct node * node, * tmp;
    struct sht * old;

    if (likely(h->old == NULL))
        return 0;

    if (pthread_spin_trylock(&h->old->global_lock) != 0)
        return 0;

    old_line = &h->old->lines[h->old->gc_index];
    pthread_rwlock_wrlock(&old_line->lock);
    for (i = 0 ; i < max_gc_num ; i++) {
        node = old_line->nodes;
        if (node == NULL) {
            h->old->gc_index++;
            if (h->old->gc_index >= h->old->size)
                break;

            pthread_rwlock_unlock(&old_line->lock);
            old_line = &h->old->lines[h->old->gc_index];
            pthread_rwlock_wrlock(&old_line->lock);
            continue;
        }

        tmp = node->next;
        new_line = &h->lines[node->hash % h->size];
        sht_insert_node(h, new_line, node);
        /* XXX cpt_insert is incr within sht_insert_node(). Keep ? */
        atomic_decr(h->cpt_insert);
        old_line->nodes = tmp;
        old_line->len--;
    }

    n = i;

    pthread_rwlock_unlock(&old_line->lock);

    if (h->old->gc_index >= h->old->size) {
        /* put on old any new hashtable operation */
        pthread_spin_lock(&h->global_lock);

        /* wait for any (other) current hashtable transaction to finish */
        while (atomic_read(h->ref) > 1)
            ;

        old = h->old;
        h->old = NULL;

        /* resume operations */
        pthread_spin_unlock(&h->global_lock);

        /* old is unreachable */
        pthread_spin_unlock(&old->global_lock);
        sht_destroy(old);
    } else {
        pthread_spin_unlock(&h->old->global_lock);
    }

    return n;
}

__attribute__((nonnull(1)))
int sht_gc(struct sht * h, int max_gc_num)
{
    int rv;

    sht_ref(h);
    rv = _sht_gc(h, max_gc_num);
    atomic_decr(h->ref);

    return rv;
}

__attribute__((nonnull(1)))
void * sht_lookup(struct sht * h, void * key, size_t keylen)
{
    void * ptr;
    uint32_t hash;
    struct line * line;

    if (unlikely(key == NULL || keylen == 0))
        return NULL;

    ptr = NULL;
    hash = h->hash(key, keylen);
    sht_ref(h);
    atomic_incr(h->cpt_lookup);

    _sht_gc(h, h->gc_num);

    line = &h->lines[hash % h->size];

    pthread_rwlock_rdlock(&line->lock);
    ptr = line_lookup(line, key, keylen, hash);
    pthread_rwlock_unlock(&line->lock);

    if (unlikely(h->old != NULL) && ptr == NULL) {
        line = &h->old->lines[hash % h->old->size];
        pthread_rwlock_rdlock(&line->lock);
        ptr = line_lookup(line, key, keylen, hash);
        pthread_rwlock_unlock(&line->lock);
    }

    atomic_decr(h->ref);

    return ptr;
}

__attribute__((nonnull(1)))
void * sht_lookup_insert(struct sht * h, void * key, size_t keylen,
        void * value)
{
    int once;
    void * ptr;
    void * bak;
    struct line * line;
    struct node * new_node;
    uint32_t hash;

    if (unlikely(key == NULL || keylen == 0))
        return NULL;

    sht_ref(h);
    atomic_incr(h->cpt_lookup);

    _sht_gc(h, h->gc_num);

    once = 1;
    new_node = NULL;
    ptr = NULL;
    hash = h->hash(key, keylen);

    /* deal with double-size */
    line = &h->lines[hash % h->size];
    if (unlikely(atomic_read(line->len) > h->max_line_depth)) {
        if (unlikely(sht_double_size(h) != 0))
            atomic_incr(h->cpt_double_size_fail);
    }

    /* handle transition old table */
    if (unlikely(ptr == NULL && h->old != NULL)) {
        line = &h->old->lines[hash % h->old->size];
        pthread_rwlock_rdlock(&line->lock);
        ptr = line_lookup(line, key, keylen, hash);
        pthread_rwlock_unlock(&line->lock);
    }

    if (ptr != NULL)
        goto exit;

    /* lookup in the current table */
    line = &h->lines[hash % h->size];
    pthread_rwlock_wrlock(&line->lock);
lookup_insert:
    ptr = line_lookup(line, key, keylen, hash);

    if (ptr != NULL) {
        pthread_rwlock_unlock(&line->lock);
        if (new_node != NULL)
            node_destroy(h->free, new_node);

        goto exit;
    }

    /* creating a node is expansive.
     * do it unlocked, then check nothing's changed
     * line entry is the same => nothing has been inserted
     * A new entry cannot be inserted in the old table
     * The table cannot have double-sized */
    if (once) {
        bak = line->nodes;
        pthread_rwlock_unlock(&line->lock);
        new_node = node_create(h, key, keylen, value);
        if (unlikely(new_node == NULL))
            goto exit;

        once = 0;
        pthread_rwlock_wrlock(&line->lock);

        if (unlikely(line->nodes != bak)) {
            bak = line->nodes;
            goto lookup_insert; /* keep line locked */
        }
    }

    assert(new_node != NULL);

    atomic_incr(h->cpt_insert);
    line_insert(line, new_node);
    if (bak != NULL)
        atomic_incr(h->cpt_collisions);

    ptr = new_node->data;
    pthread_rwlock_unlock(&line->lock);


exit:
    atomic_decr(h->ref);
    return ptr;
}

__attribute__((nonnull(1)))
int sht_remove(struct sht * h, void * key, size_t keylen)
{
    int rv;
    uint32_t hash;
    struct line * line;
    struct node * node, * prev;

    if (unlikely(key == NULL || keylen == 0))
        return -1;

    sht_ref(h);
    _sht_gc(h, h->gc_num);

    rv = -1;
    hash = h->hash(key, keylen);
    line = &h->lines[hash % h->size];
    prev = NULL;

    pthread_rwlock_wrlock(&line->lock);
    for (node = line->nodes ; node != NULL ; node = node->next) {
        if (node->hash == hash
           && likely(node->keylen == keylen
                    && memcmp(node->key, key, keylen) == 0)) {
            if (prev == NULL)
                line->nodes = node->next;
            else
                prev->next = node->next;

            node_destroy(h->free, node);
            atomic_incr(h->cpt_remove);
            atomic_decr(line->len);
            rv = 0;
            break;
        }

        prev = node;
    }

    pthread_rwlock_unlock(&line->lock);

    if (unlikely(rv != 0 && h->old)) {
        line = &h->old->lines[hash % h->old->size];
        prev = NULL;

        pthread_rwlock_wrlock(&line->lock);
        for (node = line->nodes ; node != NULL ; node = node->next) {
            if (node->hash == hash
               && likely(node->keylen == keylen
                        && memcmp(node->key, key, keylen) == 0)) {
                if (prev == NULL)
                    line->nodes = node->next;
                else
                    prev->next = node->next;

                node_destroy(h->free, node);
                atomic_incr(h->cpt_remove);
                atomic_decr(line->len);
                rv = 0;
                break;
            }

            prev = node;
        }

        pthread_rwlock_unlock(&line->lock);
    }

    atomic_decr(h->ref);

    return rv;
}

__attribute__((nonnull(1)))
void sht_dump_stats(struct sht const * h)
{
    int i;
    int num_nodes = 0;
    for (i = 0 ; i < h->size ; i++)
        num_nodes += h->lines[i].len;

    printf("number of nodes: %d\n", num_nodes);
    printf("lookups: %lu\n", h->cpt_lookup);
    printf("inserts: %lu\n", h->cpt_insert);
    printf("removes: %lu\n", h->cpt_remove);
    printf("collisions: %lu\n", h->cpt_collisions);
    printf("double-size: %lu\n", h->cpt_double_size);
    printf("failed double-size: %lu\n", h->cpt_double_size_fail);
}
