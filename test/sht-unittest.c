#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "common.h"
#include "sht.h"


static void
test_creation(void)
{
    int i;
    struct sht * h;
    int sizes[] = {-1, 0, 1, 10, 100, 1 << 10, 1 << 20};

    for (i = 0 ; i < arraylen(sizes) ; i++) {
        h = sht_create(sizes[i]);
        check(h != NULL);
        sht_destroy(h);
    }
}

static void
test_insert_lookup(void)
{
    struct sht * h;
    int * ptr;
    int rv;
    int key = 42;
    int value = 23;

    h = sht_create(10);
    check(h != NULL);

    rv = sht_insert(h, NULL, sizeof(key), &value);
    check(rv != 0);

    rv = sht_insert(h, &key, 0, &value);
    check(rv != 0);

    rv = sht_insert(h, &key, sizeof(key), &value);
    check(rv == 0);

    ptr = sht_lookup(h, &value, sizeof(value));
    check(ptr == NULL);

    ptr = sht_lookup(h, &key, sizeof(key));
    check(ptr != NULL && *ptr == value);

    sht_destroy(h);
}

static void
test_remove(void)
{
    struct sht * h;
    int * ptr;
    int rv;
    int key = 42;
    int value = 23;

    h = sht_create(10);
    check(h != NULL);

    rv = sht_insert(h, &key, sizeof(key), &value);
    check(rv == 0);

    ptr = sht_lookup(h, &value, sizeof(value));
    check(ptr == NULL);

    ptr = sht_lookup(h, &key, sizeof(key));
    check(ptr != NULL && *ptr == value);

    rv = sht_remove(h, &key, sizeof(key));
    check(rv == 0);

    ptr = sht_lookup(h, &value, sizeof(value));
    check(ptr == NULL);

    sht_destroy(h);
}

int main(void)
{
    test_creation();
    test_insert_lookup();
    test_remove();

    return 0;
}
