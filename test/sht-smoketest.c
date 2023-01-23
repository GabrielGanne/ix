#define _GNU_SOURCE /* pthread_setaffinity_np() */
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "common.h"
#include "sht.h"

/* expecting:
 * num inserts = NUM_KEYS
 * num lookups = (NUM_THREADS * NUM_INSERT + NUM_KEYS) * 2
 * ~7 double-size
 */
#define NUM_HT_LINES 10
#define NUM_KEYS 10 * 1000
#define MAX_KEYSIZE 100
#define NUM_THREADS 10
#define NUM_INSERT 100 * NUM_KEYS

static struct sht * h;

struct test_entry {
    void * key;
    void * value;
    size_t keylen;
};
static struct test_entry ** test_values;

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
    check(test_values != NULL);

    for (i = 0 ; i < NUM_KEYS ; i++) {
        e = malloc(sizeof(*e));
        check(e != NULL);
        key = malloc(sizeof(int));
        check(key != NULL);
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
    void * ptr;
    struct test_entry * e;

    (void) void_args;

    /* all threads insert all keys once (the same at about the same time) */
    for (i = 0 ; i < NUM_KEYS ; i++) {
        e = test_values[i];
        ptr = sht_lookup_insert(h, e->key, e->keylen, e->value);
        check(ptr == e->value);
    }

    return NULL;
}

int main(void)
{
    int rv, i;
    void * ptr;
    struct test_entry * e;
    cpu_set_t cpuset;
    pthread_t threads[NUM_THREADS] = {0};
    void * thread_rv[NUM_THREADS];

    h = sht_create(NUM_HT_LINES);
    check(h != NULL);

    srand(0);
    init_test_values();

    CPU_ZERO(&cpuset);
    for (i = 0 ; i < NUM_THREADS ; i++) {
        rv = pthread_create(&threads[i], NULL, &sht_test_thread, NULL);
        check(rv == 0);
        CPU_SET(i, &cpuset);
        rv = pthread_setaffinity_np(threads[i], sizeof(cpu_set_t), &cpuset);
        check(rv == 0);
    }

    /* join all threads */
    for (i = 0 ; i < NUM_THREADS ; i++) {
        rv = pthread_join(threads[i], (void **) &thread_rv[i]);
        check(rv == 0);
        check(thread_rv[i] == NULL);
    }

    /* test all the entries have been inserted */
    for (i = 0 ; i < NUM_KEYS ; i++) {
        e = test_values[i];
        ptr = sht_lookup(h, e->key, e->keylen);
        check(ptr == e->value);
    }

    /* dump stats */
    printf("### dump full hashtable\n");
    sht_dump_stats(h);

    /* remove all the values, check it cannot be found anymore */
    for (i = 0 ; i < NUM_KEYS ; i++) {
        e = test_values[i];
        rv = sht_remove(h, e->key, e->keylen);
        check(rv == 0);

        ptr = sht_lookup(h, e->key, e->keylen);
        check(ptr == NULL);
    }

    /* dump stats */
    printf("### dump empty hashtable\n");
    sht_dump_stats(h);

    deinit_test_values();
    sht_destroy(h);

    return 0;
}
