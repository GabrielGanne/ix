#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "timer-wheel.h"
#include "check.h"

/* A simple counter to track how many times our callback is invoked. */
static int g_callback_fired_count = 0;
/* A variable to store the data from the last callback invocation. */
static int g_last_callback_data = 0;

/* This callback function will be used by the timers in our tests. */
static
void test_callback(void * data)
{
    g_callback_fired_count++;
    /* We cast the data to an int for verification purposes. */
    if (data) {
        g_last_callback_data = *(int*)data;
    }
}

/* Resets the global counters before each test case. */
static
void reset_globals(void)
{
    g_callback_fired_count = 0;
    g_last_callback_data = 0;
}

/* Verify creation and destruction of the timer wheel. */
static
void test_create_and_destroy(void)
{
    struct timer_wheel * tw;
    reset_globals();

    tw = timer_wheel_create_custom(1024, 1000, test_callback, NULL, NULL);
    check(tw != NULL);
    timer_wheel_destroy(tw, 1);

    tw = timer_wheel_create_custom(17, 1000, test_callback, NULL, NULL);
    check(tw != NULL);
    timer_wheel_destroy(tw, 1);

    tw = timer_wheel_create(42, 1000, NULL);
    check(tw != NULL);
    timer_wheel_destroy(tw, 1);
}

/* Add a single timer and ensure it fires after a tick. */
static
void test_add_and_tick_simple(void)
{
    int rv;
    reset_globals();

    uint64_t tick_res_ns = 1000000; /* 1 ms */
    struct timer_wheel * tw = timer_wheel_create_custom(16, tick_res_ns,
            test_callback, NULL, NULL);
    check(tw != NULL);

    int data = 42;
    uint64_t delay_ns = 500000; /* 0.5 ms (should fire on the first tick) */

    rv = timer_wheel_add(tw, delay_ns, &data);
    check(rv == 0);
    check(g_callback_fired_count == 0);

    /* Tick the wheel past the timer's expiration. */
    timer_wheel_tick(tw, tick_res_ns);
    check(g_callback_fired_count == 1);
    check(g_last_callback_data == 42);

    timer_wheel_destroy(tw, 1);
}

/* Add a timer and ensure it does not fire prematurely. */
static
void test_timer_does_not_fire_early(void)
{
    int rv;
    reset_globals();

    uint64_t tick_res_ns = 1000; /* 1 us */
    struct timer_wheel * tw = timer_wheel_create_custom(32, tick_res_ns,
            test_callback, NULL, NULL);
    check(tw != NULL);

    int data = 99;
    uint64_t delay_ns = 5 * tick_res_ns; /* Expires after 5 ticks */

    rv = timer_wheel_add(tw, delay_ns, &data);
    check(rv == 0);

    /* Tick 4 times. The timer should not have fired yet. */
    for (int i = 1 ; i <= 4 ; ++i) {
        timer_wheel_tick(tw, i * tick_res_ns);
        check(g_callback_fired_count == 0);
    }

    /* The 5th tick should fire the timer. */
    timer_wheel_tick(tw, 5 * tick_res_ns);
    check(g_callback_fired_count == 1);
    check(g_last_callback_data == 99);

    timer_wheel_destroy(tw, 1);
}


/* Add multiple timers and ensure they fire in the correct order. */
static
void test_multiple_timers(void)
{
    int rv;
    reset_globals();

    uint64_t tick_res_ns = 1000;
    struct timer_wheel * tw = timer_wheel_create_custom(64, tick_res_ns,
            test_callback, NULL, NULL);
    check(tw != NULL);

    int data1 = 1, data2 = 2, data3 = 3;

    /* Add three timers with different delays. */
    rv = timer_wheel_add(tw, 3 * tick_res_ns, &data3); /* Fires at tick 3 */
    check(rv == 0);
    rv = timer_wheel_add(tw, 1 * tick_res_ns, &data1); /* Fires at tick 1 */
    check(rv == 0);
    rv = timer_wheel_add(tw, 2 * tick_res_ns, &data2); /* Fires at tick 2 */
    check(rv == 0);

    /* Tick 1: timer 1 should fire. */
    timer_wheel_tick(tw, 1 * tick_res_ns);
    check(g_callback_fired_count == 1);
    check(g_last_callback_data == 1);

    /* Tick 2: timer 2 should fire. */
    timer_wheel_tick(tw, 2 * tick_res_ns);
    check(g_callback_fired_count == 2);
    check(g_last_callback_data == 2);

    /* Tick 3: timer 3 should fire. */
    timer_wheel_tick(tw, 3 * tick_res_ns);
    check(g_callback_fired_count == 3);
    check(g_last_callback_data == 3);

    /* Tick 4: nothing should fire. */
    timer_wheel_tick(tw, 4 * tick_res_ns);
    check(g_callback_fired_count == 3);

    timer_wheel_destroy(tw, 1);
}

/* Test a timer that requires the wheel to wrap around. */
static
void test_timer_wrapping(void)
{
    int rv;
    reset_globals();

    uint32_t wheel_size = 16;
    uint64_t tick_res_ns = 1000;
    struct timer_wheel * tw = timer_wheel_create_custom(wheel_size, tick_res_ns,
            test_callback, NULL, NULL);
    check(tw != NULL);

    int data = 77;
    /* Delay is larger than one full revolution of the wheel. */
    uint64_t delay_ns = (wheel_size + 5) * tick_res_ns;

    rv = timer_wheel_add(tw, delay_ns, &data);
    check(rv == 0);

    /* Tick through one full revolution. Nothing should fire. */
    for (uint64_t i = 1 ; i <= wheel_size ; ++i) {
        timer_wheel_tick(tw, i * tick_res_ns);
    }

    check(g_callback_fired_count == 0);

    /* Tick up to the expiration time. */
    for (uint64_t i = wheel_size + 1 ; i < wheel_size + 5 ; ++i) {
        timer_wheel_tick(tw, i * tick_res_ns);
        check(g_callback_fired_count == 0);
    }

    /* This tick should finally fire the timer. */
    timer_wheel_tick(tw, (wheel_size + 5) * tick_res_ns);
    check(g_callback_fired_count == 1);
    check(g_last_callback_data == 77);

    timer_wheel_destroy(tw, 1);
}


int main(void)
{
    test_create_and_destroy();
    test_add_and_tick_simple();
    test_timer_does_not_fire_early();
    test_multiple_timers();
    test_timer_wrapping();

    return 0;
}
