#define _GNU_SOURCE /* pthread_setaffinity_np() */
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "common.h"
#include "pqueue.h"

#define CAPACITY 64
#define NUM_THREADS 10
#define NUM_INSERT 100

static struct pq * q;

struct test_entry {
    void * value;
    uint64_t ttl;
    int cpt_expired;
};
static struct test_entry ** test_values;
static struct test_entry ** test_expired_values;

static inline uint64_t random_ttl(void)
{
    return rand() % 1000;
}

static void
init_test_values(void)
{
    int i;
    struct test_entry * e;

    test_values = malloc(CAPACITY * sizeof(*test_values));
    check(test_values != NULL);

    test_expired_values = malloc(CAPACITY * sizeof(*test_expired_values));
    check(test_values != NULL);

    for (i = 0 ; i < CAPACITY ; i++) {
        e = malloc(sizeof(*e));
        check(e != NULL);
        *e = (struct test_entry) {
            .value = (void*)(uintptr_t)i,
            .ttl = random_ttl(),
        };

        test_values[i] = e;
        test_expired_values[i] = e;
    }
}

static void expire_fn(void * value)
{
    uintptr_t idx = (uintptr_t)value;
    test_expired_values[idx]->cpt_expired++;
}

static void
deinit_test_values(void)
{
    int i;

    struct test_entry * e;

    for (i = 0 ; i < CAPACITY ; i++) {
        e = test_values[i];
        free(e);
    }

    free(test_values);
    free(test_expired_values);
}

static void *
pq_test_thread(void * void_args)
{
    int i;
    int rv;
    struct test_entry * e;

    (void) void_args;

    /* all threads insert all keys once (the same at about the same time) */
    for (i = 0 ; i < NUM_INSERT * CAPACITY ; i++) {
        e = test_values[i % CAPACITY];
        rv = pq_insert(q, i, e->value, e->ttl);
        check(rv == 0);
        rv = pq_expire_all(q, i);
        check(rv == 0);
    }

    return NULL;
}

int main(void)
{
    int rv, i;
    cpu_set_t cpuset;
    pthread_t threads[NUM_THREADS] = {0};
    void * thread_rv[NUM_THREADS];

    q = pq_create(CAPACITY, expire_fn);
    check(q != NULL);

    srand(0);
    init_test_values();

    CPU_ZERO(&cpuset);
    for (i = 0 ; i < NUM_THREADS ; i++) {
        rv = pthread_create(&threads[i], NULL, &pq_test_thread, NULL);
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

    /* dump stats */
    printf("### dump priority queue\n");
    pq_dump_stats(q);

    /* this should expire all entries */
    pq_destroy(q);

    /* check all the test entries have been expired NUM_THREADS times */
    for (i = 0 ; i < CAPACITY ; i++) {
        check(test_expired_values[i]->cpt_expired == NUM_THREADS * NUM_INSERT);
    }

    deinit_test_values();

    return 0;
}
