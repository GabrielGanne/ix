#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "timer-wheel.h"

#define TW_DEFAULT_SIZE 256
#define TW_DEFAULT_RESOLUTION 1000 /* 1ms resolution */

typedef struct timer_wheel_node {
    struct timer_wheel_node * next;
    struct timer_wheel_node * prev;
    uint64_t expiry;
    void * data;
} timer_node_t;

/*
 * Slot locks are used to protect from concurrent insertion.
 */
typedef struct timer_wheel {
    struct timer_wheel_node ** slots;
    pthread_mutex_t * lock; /* slot lock array */
    uint32_t size;
    uint32_t mask;
    uint64_t current_tick;
    uint64_t tick_resolution; /* nanoseconds per tick */
    tw_callback expire_cb;

    tw_alloc_fn malloc;
    tw_free_fn free;

    /* stats */
    uint64_t cpt_expired;
    uint64_t cpt_add;
    uint64_t cpt_add_expired;
    uint64_t cpt_timer_loop;
} timer_wheel_t;


__attribute__((nonnull(1, 2)))
static inline
void list_add(struct timer_wheel_node ** head,
        struct timer_wheel_node * node)
{
    if (*head == NULL) {
        node->next = node->prev = node;
        *head = node;
    } else {
        node->next = *head;
        node->prev = (*head)->prev;
        (*head)->prev->next = node;
        (*head)->prev = node;
    }
}

__attribute__((nonnull(1, 2)))
static inline
int list_remove(struct timer_wheel_node ** head,
        struct timer_wheel_node * node)
{
    if (unlikely(*head == NULL))
        return -1;

    if (node->next == node) {
        *head = NULL;
    } else {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        if (*head == node) {
            *head = node->next;
        }
    }

    node->next = node->prev = NULL;
    return 0;
}

void timer_wheel_destroy(struct timer_wheel * tw, int do_callback)
{
    struct timer_wheel_node * head;
    struct timer_wheel_node * next;

    if (tw == NULL)
        return;

    if (likely(tw->slots != NULL)) {
        for (uint32_t i = 0 ; i < tw->size ; i++) {
            pthread_mutex_lock(&tw->lock[i]);
            head = tw->slots[i];

            if (head != NULL)
                head->prev->next = NULL;

            while (head != NULL) {
                next = head->next;
                if (do_callback)
                    tw->expire_cb(head->data);

                tw->free(head);
                head = next;
            }

            pthread_mutex_unlock(&tw->lock[i]);
        }

        tw->free(tw->slots);
    }

    if (tw->lock != NULL) {
        for (uint32_t i = 0 ; i < tw->size ; i++) {
            pthread_mutex_destroy(&tw->lock[i]);
        }

        tw->free(tw->lock);
    }

    tw->free(tw);
}

static ALWAYS_INLINE
bool is_power_of_2(uint32_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

static ALWAYS_INLINE
uint32_t timer_wheel_next_power_of_2(uint32_t n)
{
    if (n == 0)
        return 1;

    if (is_power_of_2(n))
        return n;

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

struct timer_wheel * timer_wheel_create_custom(uint32_t size,
        uint64_t tick_resolution_ns, tw_callback cb,
        tw_alloc_fn alloc_fn, tw_free_fn free_fn)
{
    struct timer_wheel * tw;
    struct timer_wheel_node ** slots;
    pthread_mutex_t * locks;

    if (alloc_fn == NULL)
        alloc_fn = malloc;

    if (free_fn == NULL)
        free_fn = free;

    if (tick_resolution_ns == 0)
        tick_resolution_ns = TW_DEFAULT_RESOLUTION;

    if (size == 0)
        size = TW_DEFAULT_SIZE;

    /* Ensure wheel size is power of 2 */
    if (!is_power_of_2(size))
        size = timer_wheel_next_power_of_2(size);

    tw = alloc_fn(sizeof(*tw));
    if (unlikely(tw == NULL))
        return NULL;

    /* Allocate slots array */
    slots = alloc_fn(size * sizeof(*tw->slots));
    if (unlikely(slots == NULL)) {
        free_fn(tw);
        return NULL;
    }

    memset(slots, 0, size * sizeof(*tw->slots));

    locks = alloc_fn(size * sizeof(*locks));
    if (unlikely(locks == NULL)) {
        free_fn(tw);
        free_fn(slots);
        return NULL;
    }

    memset(locks, 0, size * sizeof(*locks));

    *tw = (struct timer_wheel) {
        .slots = slots,
        .lock = locks,
        .size = size,
        .mask = size - 1,
        .current_tick = 0,
        .tick_resolution = tick_resolution_ns,
        .expire_cb = cb,
        .malloc = alloc_fn,
        .free = free_fn,
    };

    for (uint32_t i = 0 ; i < size ; i++) {
        pthread_mutex_init(&tw->lock[i], NULL);
    }

    return tw;
}

__attribute__((nonnull(1)))
int timer_wheel_add(struct timer_wheel * tw, uint64_t delay_ns, void * data)
{
    uint64_t ticks_delay;
    uint64_t expiry_tick;

    atomic_incr(tw->cpt_add);

    if (unlikely(delay_ns == 0))
        return 0;

    struct timer_wheel_node * timer = tw->malloc(sizeof(*timer));
    if (unlikely(!timer))
        return -1;

    ticks_delay = (delay_ns + tw->tick_resolution - 1) / tw->tick_resolution;
    expiry_tick = atomic_read(tw->current_tick) + ticks_delay;

    *timer = (struct timer_wheel_node) {
        .expiry = expiry_tick * tw->tick_resolution,
        .data = data,
        .next = NULL,
        .prev = NULL,
    };

    uint32_t slot = expiry_tick & tw->mask;
    pthread_mutex_lock(&tw->lock[slot]);

    /*
     * In the odd case where the insertion time was longer than the ttl
     * fire the timer right away to prevent being a full wheel turn late.
     * This is not an error.
     */
    if (unlikely(expiry_tick < atomic_read(tw->current_tick) + ticks_delay)) {
        pthread_mutex_unlock(&tw->lock[slot]);
        tw->expire_cb(timer->data);
        tw->free(timer);
        atomic_incr(tw->cpt_add_expired);
        return 0;
    }

    list_add(&tw->slots[slot], timer);
    pthread_mutex_unlock(&tw->lock[slot]);

    return 0;
}

__attribute__((nonnull(1)))
int timer_wheel_tick(struct timer_wheel * tw, uint64_t current_time_ns)
{
    int expired_cpt;
    uint64_t target_tick;
    uint64_t tick;
    struct timer_wheel_node * timer;
    struct timer_wheel_node * head;

    if (unlikely(tw->slots == NULL))
        return -1;

    /* Do not go back in time. This is not an error, let's just wait a bit. */
    if (current_time_ns < atomic_read(tw->current_tick))
        return 0;

    expired_cpt = 0;
    target_tick = current_time_ns / tw->tick_resolution;
    tick = atomic_read(tw->current_tick);
    while (tick <= target_tick) {
        uint32_t slot = tick & tw->mask;

        pthread_mutex_lock(&tw->lock[slot]);
        head = tw->slots[slot];
        tw->slots[slot] = NULL;
        pthread_mutex_unlock(&tw->lock[slot]);

        while (head) {
            timer = head;

            /* Remove from circular list */
            list_remove(&head, timer);

            /* Check if timer has actually expired */
            if (likely(timer->expiry <= current_time_ns)) {
                tw->expire_cb(timer->data);
                tw->free(timer);
                expired_cpt++;
            } else {
                /* Re-insert timer (for multi-round timers) */
                uint64_t expiry_tick = timer->expiry / tw->tick_resolution;
                uint32_t new_slot = expiry_tick & tw->mask;
                pthread_mutex_lock(&tw->lock[new_slot]);
                list_add(&tw->slots[new_slot], timer);
                pthread_mutex_unlock(&tw->lock[new_slot]);
                atomic_incr(tw->cpt_timer_loop);
            }
        }

        atomic_incr(tw->current_tick);
        tick++;
    }

    __atomic_fetch_add(&tw->cpt_expired, expired_cpt, __ATOMIC_SEQ_CST);

    return expired_cpt;
}

__attribute__((nonnull(1)))
void timer_wheel_dump_stats(struct timer_wheel const * tw)
{
    printf("added: %lu\n", tw->cpt_add);
    printf("expired: %lu\n", tw->cpt_expired);
    printf("timer_loop: %lu\n", tw->cpt_timer_loop);
    printf("add_expired: %lu\n", tw->cpt_add_expired);
}
