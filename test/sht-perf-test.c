#define _GNU_SOURCE /* pthread_setaffinity_np() */
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "sht.h"

#define NUM_HT_LINES (1 << 10)
#define NUM_KEYS (NUM_HT_LINES << 4)
#define NUM_THREADS 100
#define NUM_ACTIONS (NUM_KEYS << 4)

static struct sht * h;

struct test_entry {
    void * key;
    void * value;
    size_t keylen;
};
static struct test_entry ** test_values;

static inline int random_int(int max)
{
    return rand() % max;
}

static inline void * random_ptr(void)
{
    return (void*) (uintptr_t) rand();
}

static void
init_test_values(void)
{
    int i;
    void * key;
    struct test_entry * e;

    test_values = malloc(NUM_KEYS * sizeof(*test_values));

    for (i = 0 ; i < NUM_KEYS ; i++) {
        e = malloc(sizeof(*e));
        key = malloc(sizeof(int));
        *(int*) key = i;

        *e = (struct test_entry) {
            .key = key,
            .value = random_ptr(),
            .keylen = sizeof(int),
        };

        test_values[i] = e;
    }
}

static void
deinit_test_values(void)
{
    int i;
    struct test_entry * e;

    for (i = 0 ; i < NUM_KEYS ; i++) {
        e = test_values[i];
        free(e->key);
        free(e);
    }

    free(test_values);
}

static void *
sht_test_thread(void * void_args)
{
    int i;
    struct test_entry * e;

    (void) void_args;

    /* all threads insert all keys once (the same at about the same time) */
    for (i = 0 ; i < NUM_ACTIONS ; i++) {
        e = test_values[random_int(NUM_KEYS)];
        switch (random_int(4)) {
        case 0:
            sht_remove(h, e->key, e->keylen);
            break;
        case 1:
            sht_insert(h, e->key, e->keylen, e->value);
            break;
        case 2:
            sht_lookup(h, e->key, e->keylen);
            break;
        case 3:
            sht_lookup_insert(h, e->key, e->keylen, e->value);
            break;
        }
    }

    return NULL;
}

/* Dummy hash function so that the cost of hashing is at its lowest
 * by construction, all the hash values will be different, and keylen
 * will always equal 4 */
static
uint32_t dummy_hash(void * data, size_t len)
{
    (void) len;
    return *(uint32_t*) data;
}


int main(void)
{
    int i;
    cpu_set_t cpuset;
    pthread_t threads[NUM_THREADS] = {0};
    void * thread_rv[NUM_THREADS];

    h = sht_create_custom(NUM_HT_LINES, NULL, NULL, dummy_hash);

    srand(0);
    init_test_values();

    CPU_ZERO(&cpuset);
    for (i = 0 ; i < NUM_THREADS ; i++) {
        pthread_create(&threads[i], NULL, &sht_test_thread, NULL);
        CPU_SET(i, &cpuset);
        pthread_setaffinity_np(threads[i], sizeof(cpu_set_t), &cpuset);
    }

    /* join all threads */
    for (i = 0 ; i < NUM_THREADS ; i++)
        pthread_join(threads[i], (void **) &thread_rv[i]);

    /* dump stats */
    printf("### dump hashtable\n");
    sht_dump_stats(h);

    deinit_test_values();
    sht_destroy(h);

    return 0;
}
