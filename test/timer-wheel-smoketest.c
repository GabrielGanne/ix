#define _GNU_SOURCE /* pthread_setaffinity_np() */
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "common.h"
#include "timer-wheel.h"

#define CAPACITY 64
#define NUM_THREADS 10
#define NUM_INSERT 100

static struct timer_wheel * tw;
static uint64_t global_time = 0;

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
    atomic_incr(test_expired_values[idx]->cpt_expired);
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
tw_test_thread(void * void_args)
{
    int i;
    int rv;
    struct test_entry * e;

    (void) void_args;

    /* all threads insert all keys once (the same at about the same time) */
    for (i = 0 ; i < NUM_INSERT * CAPACITY ; i++) {
        e = test_values[i % CAPACITY];
        rv = timer_wheel_add(tw, e->ttl, e->value);
        check(rv == 0);
        atomic_incr(global_time);
        rv = timer_wheel_tick(tw, atomic_read(global_time));
        check(rv >= 0);
    }

    return NULL;
}

int main(void)
{
    int rv, i;
    cpu_set_t cpuset;
    pthread_t threads[NUM_THREADS] = {0};
    void * thread_rv[NUM_THREADS];

    tw = timer_wheel_create(CAPACITY, 1000, expire_fn);
    check(tw != NULL);

    srand(0);
    init_test_values();

    CPU_ZERO(&cpuset);
    for (i = 0 ; i < NUM_THREADS ; i++) {
        rv = pthread_create(&threads[i], NULL, &tw_test_thread, NULL);
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
    printf("### dump timer-wheel stats\n");
    timer_wheel_dump_stats(tw);

    /* this should expire all entries */
    timer_wheel_destroy(tw, 1);

    /* check all the test entries have been expired NUM_THREADS times */
    for (i = 0 ; i < CAPACITY ; i++) {
        check(test_expired_values[i]->cpt_expired == NUM_THREADS * NUM_INSERT);
    }

    deinit_test_values();

    return 0;
}
