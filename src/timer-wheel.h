#ifndef TIMER_WHEEL_H
#define TIMER_WHEEL_H

#include <stdint.h>

typedef void * (* tw_alloc_fn)(size_t size);
typedef void (* tw_free_fn)(void * ptr);

struct timer_wheel;
typedef void (*tw_callback)(void *);

/* size will be rounded up to the next power of two */
struct timer_wheel * timer_wheel_create_custom(uint32_t wheel_size,
        uint64_t tick_resolution_ns, tw_callback cb,
        tw_alloc_fn alloc_fn, tw_free_fn free_fn);
#define timer_wheel_create(size, tick_resolution_ns, cb) \
        timer_wheel_create_custom(size, tick_resolution_ns, cb, NULL, NULL)

void timer_wheel_destroy(struct timer_wheel * tw, int do_callback);

/* Add a timer */
int timer_wheel_add(struct timer_wheel * tw, uint64_t delay_ns, void * data);

/* Process expired timers.
 * Design exects a single thread is ticking the lock
 *
 * Returns the number of expired timers on success, a negative value on error */
int timer_wheel_tick(struct timer_wheel * tw, uint64_t current_time_ns);

void timer_wheel_dump_stats(struct timer_wheel const * tw);

#endif /* TIMER_WHEEL_H */
