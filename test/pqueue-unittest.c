#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "common.h"
#include "pqueue.h"


static void
test_creation(void)
{
    int i;
    struct pq * q;
    int sizes[] = {-1, 0, 1, 10, 100, 1 << 10, 1 << 20};

    for (i = 0 ; i < arraylen(sizes) ; i++) {
        q = pq_create(sizes[i], NULL);
        check(q != NULL);
        pq_destroy(q);
    }
}

static void
test_insert(void)
{
    int rv;
    struct pq * q;
    int value = 0;

    q = pq_create(0, NULL);
    check(q != NULL);

    rv = pq_insert(q, 0, NULL, 0);
    check(rv == 0);

    rv = pq_insert(q, 0, NULL, 60);
    check(rv == 0);

    rv = pq_insert(q, 0, &value, 60);
    check(rv == 0);

    rv = pq_insert(q, 123, &value, 60);
    check(rv == 0);

    pq_destroy(q);
}

static int expire_cpt = 0;
static int value = 42;
static inline
void test_cb(void * ptr)
{
    int * v = (int *)ptr;
    check(v == NULL || *v == value);

    expire_cpt++;
}

static void
test_expire(void)
{
    int rv;
    struct pq * q;

    q = pq_create(0, test_cb);
    check(q != NULL);

    rv = pq_insert(q, 0, &value, 0);
    check(rv == 0);

    rv = pq_expire(q, 0, 0);  /* does nothing */
    check(rv == 0);
    check(expire_cpt == 0);

    rv = pq_expire(q, 1, 1);  /* expires */
    check(rv == 0);
    check(expire_cpt == 1);

    rv = pq_expire(q, 1, 1);  /* expires nothing */
    check(rv == 0);
    check(expire_cpt == 1);

    rv = pq_insert(q, 10, &value, 10);
    check(rv == 0);

    /* expires nothing */
    rv = pq_expire(q, 10, 10);
    check(rv == 0);
    check(expire_cpt == 1);

    rv = pq_expire(q, 19, 10);
    check(rv == 0);
    check(expire_cpt == 1);

    rv = pq_expire(q, 20, 10);  /* expires */
    check(rv == 0);
    check(expire_cpt == 2);

    rv = pq_insert(q, 1234, &value, 42);
    check(rv == 0);
    rv = pq_expire(q, 5678, 10);  /* expires */
    check(rv == 0);
    check(expire_cpt == 3);

    pq_destroy(q);
}

static void
test_expire_all(void)
{
    int rv;
    struct pq * q;

    q = pq_create(0, test_cb);
    check(q != NULL);
    expire_cpt = 0;

    rv = pq_insert(q, 0, &value, 42);
    check(rv == 0);
    rv = pq_insert(q, 10, &value, 142);
    check(rv == 0);
    rv = pq_insert(q, 20, &value, 8888);
    check(rv == 0);

    rv = pq_expire_all(q, 10);
    check(rv == 0);
    check(expire_cpt == 0);

    rv = pq_expire_all(q, 10000);
    check(rv == 0);
    check(expire_cpt == 3);

    pq_destroy(q);
}

static void
test_pq_item_create(void)
{
    struct pq * q;
    struct pq_item * item;

    q = pq_create(0, test_cb);
    check(q != NULL);
    item = pq_item_create(q, 10, NULL);
    check(item != NULL);

    pq_item_destroy(q, item);

    pq_destroy(q);
}

static void
test_pq_item_insert(void)
{
    int rv;
    struct pq * q;
    struct pq_item * item;

    q = pq_create(0, test_cb);
    check(q != NULL);
    item = pq_item_create(q, 10, NULL);
    check(item != NULL);

    rv = pq_item_insert(q, item);
    check(rv == 0);

    rv = pq_item_remove(q, item);
    check(rv == 0);

    pq_item_destroy(q, item);
    pq_destroy(q);
}

static void
test_pq_item_resched(void)
{
    int rv;
    struct pq * q;
    struct pq_item * item;

    q = pq_create(0, test_cb);
    check(q != NULL);
    item = pq_item_create(q, 10, NULL);
    check(item != NULL);

    rv = pq_item_insert(q, item);
    check(rv == 0);

    rv = pq_item_resched(q, 20, item, 20);
    check(rv == 0);

    expire_cpt = 0;
    rv = pq_expire_all(q, 30);
    check(rv == 0);
    check(expire_cpt == 0);

    rv = pq_expire_all(q, 50);
    check(rv == 0);
    check(expire_cpt == 1);

    pq_destroy(q);
}

int main(void)
{
    test_creation();
    test_insert();
    test_expire();
    test_expire_all();

    test_pq_item_create();
    test_pq_item_insert();
    test_pq_item_resched();

    return 0;
}
