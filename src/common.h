#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

/* config values */

#define LG2_CACHELINE_SIZE CONFIG_LOG2_CPU_CACHELINE_SIZE
#define CACHELINE_SIZE (1 << LG2_CACHELINE_SIZE)

#define LG2_PAGE_SIZE CONFIG_LOG2_CPU_PAGE_SIZE
#define PAGE_SIZE (1 << LG2_PAGE_SIZE)

/* common macros */

#define arraylen(x) ((int) (sizeof(x) / sizeof(*(x))))

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define likely(expr)   __builtin_expect(!!(expr), 1)
#define unlikely(expr) __builtin_expect(!!(expr), 0)

#define ALWAYS_INLINE inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))
#define PACKED __attribute__((packed))
#define CACHE_ALIGNED __attribute__((aligned(CACHELINE_SIZE)))

#define atomic_incr(value) \
    __atomic_fetch_add(&value, 1, __ATOMIC_SEQ_CST)

#define atomic_decr(value) \
    __atomic_fetch_sub(&value, 1, __ATOMIC_SEQ_CST)

/* silence warnings about void const */
#define VOIDPTR(ptr) \
    (void *)(uintptr_t)(ptr)

/* common function typedefs */
typedef uint32_t (* hash_fn) (void * data, size_t datalen);

/* "one-at-a-time" generic hash function used by jenkins
 * used as default function hash if none provided */
static inline
uint32_t oat_hash(void * data, size_t len)
{
    const uint8_t * p = data;
    uint64_t h = 0;
    size_t i;

    for (i = 0 ; i < len ; i++) {
        h += p[i];
        h += (h << 10);
        h ^= (h >> 6);
    }

    h += (h << 3);
    h ^= (h >> 11);
    h += (h << 15);

    return h;
}

#endif /* COMMON_H */
