/*******************************************************************************
 * Copyright (c) 2017 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************/
#include <err.h>
#include <arch/arch_ops.h>
#include <arch/local_apic.h>
#include <platform/sand.h>
#include <platform/interrupts.h>
#include <platform/timer.h>
#include <debug.h>

#if WITH_SM_WALL
#include <lib/sm.h>
#include <lib/sm/sm_wall.h>
#include <lk/init.h>

/* Backup timer definition aligns to sec_timer_state in smcall
 *
 * struct backup_timer - structure to hold secute timer state
 * @tv_ns:      If non-zero this field contains snapshot of timers
 *              current time (ns).
 * @cv_ns:      next timer event configured (ns)
 */
typedef struct bakcup_timer_t {
    uint64_t tv_ns;
    uint64_t cv_ns;
}backup_timer;

#define MS_TO_NS(ms) ((ms)*1000000ULL)
backup_timer back_timer;
#endif

static volatile uint64_t timer_current_time; /* in ms */
static uint64_t timer_delta_time; /* in ms */

static platform_timer_callback t_callback;
static void *callback_arg;

lk_time_t current_time(void)
{
    uint64_t val;
    uint32_t low, high;

    rdtsc(low, high);
    val = (uint64_t)((uint64_t)high <<32 | (uint64_t)low);
    timer_current_time = val/(uint64_t)(g_trusty_startup_info.calibrate_tsc_per_ms);

    return timer_current_time;
}

lk_bigtime_t current_time_hires(void)
{
    return timer_current_time * 1000;
}

#if WITH_SM_WALL

static void update_bakcup_timer(uint64_t tv, uint64_t cv)
{
    back_timer.tv_ns = MS_TO_NS(tv);
    back_timer.cv_ns = MS_TO_NS(cv);
    return;
}

/* Timer will be started at Android side if tv bigger than cv */
inline static void set_backup_timer(uint64_t interval)
{
    update_bakcup_timer(0, interval);
}

/* Timer will be stopped at Android side if cv bigger than tv */
inline static void stop_backup_timer(void)
{
    update_bakcup_timer(1, 0);
}

static void update_wall_cb(struct sm_wall_item *wi, void *item)
{
    struct sec_timer_state *wall_tm = item;

    wall_tm->tv_ns = back_timer.tv_ns;
    wall_tm->cv_ns = back_timer.cv_ns;

    return;
}

static struct sm_wall_item timer_wall_item[SMP_MAX_CPUS] = {
    [0 ... SMP_MAX_CPUS-1] =
    SM_WALL_ITEM_INITIALIZE(SM_WALL_PER_CPU_SEC_TIMER_ID, update_wall_cb,
        sizeof(struct bakcup_timer_t)),
};

static void reg_timer_wall_item(uint lvl)
{
    sm_wall_register_per_cpu_item(&timer_wall_item[arch_curr_cpu_num()]);
}

LK_INIT_HOOK_FLAGS(timer, reg_timer_wall_item, LK_INIT_LEVEL_PLATFORM + 1, LK_INIT_FLAG_ALL_CPUS);

#endif

#if PLATFORM_HAS_DYNAMIC_TIMER
status_t platform_set_oneshot_timer(platform_timer_callback callback,
        void *arg, lk_time_t interval)
{
    t_callback = callback;
    callback_arg = arg;
    timer_delta_time = interval;

#ifdef WITH_SM_WALL
    if(is_lk_boot_complete()) {
        set_backup_timer(interval);
    }
#endif

    return NO_ERROR;
}

void platform_stop_timer(void)
{
#ifdef WITH_SM_WALL
    if(is_lk_boot_complete()) {
        stop_backup_timer();
    }
#endif

    return;
}

#else
status_t platform_set_periodic_timer(platform_timer_callback callback,
        void *arg, lk_time_t interval)
{
    t_callback = callback;
    callback_arg = arg;
    timer_delta_time = interval;

    return NO_ERROR;
}
#endif

static enum handler_return os_timer_tick(void *arg)
{
    enum handler_return ret = INT_NO_RESCHEDULE;

    /*
     * Soft interrupt is triggered for timer in current solution,
     * when handling timer interrupt, we need to check whether corresponding bit
     * of vector is set in Local APIC ISR. Since if bit set in ISR,
     * it means this interrupt is triggered by external interrupt which belongs
     * to Non-Secure world, need to redirect this interrupt to NS world.
     */
    if (local_apic_vector_in_service(INT_PIT)) {
        dprintf(CRITICAL, "WARNING: Trusty OS timer vector overlapped!!\n");
        FW_INT_TO_NS(INT_PIT);
        ret = sm_handle_irq();
    } else {
        if (!t_callback)
            return INT_NO_RESCHEDULE;

#if !PLATFORM_HAS_DYNAMIC_TIMER
        timer_current_time += timer_delta_time;
#endif
        lk_time_t time = current_time();

        ret = t_callback(callback_arg, time);
    }

    return ret;
}

void platform_init_timer(void)
{
#if !PLATFORM_HAS_DYNAMIC_TIMER
    timer_current_time = 0;
#else
    lk_time_t time = current_time();

    timer_current_time = time;
#endif

    register_int_handler(INT_PIT, &os_timer_tick, NULL);
    unmask_interrupt(INT_PIT);
}

