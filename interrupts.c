/*******************************************************************************
 * Copyright (c) 2015 Intel Corporation
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
#include <lib/sm.h>
#include <arch/local_apic.h>
#ifdef ENABLE_FPU
#include <arch/fpu.h>
#endif
#include <kernel/thread.h>
#include <platform/interrupts.h>
#include <platform/sand.h>
#include <lk/init.h>

static spin_lock_t lock;

void x86_gpf_handler(x86_iframe_t *frame);
void x86_invop_handler(x86_iframe_t *frame);
void x86_unhandled_exception(x86_iframe_t *frame);
#ifdef ARCH_X86_64
void x86_pfe_handler(x86_iframe_t *frame);
#endif

struct int_handler_struct {
    int_handler handler;
    void *arg;
};

static struct int_handler_struct int_handler_table[INT_VECTORS];

#define PIC1_DATA 0x21
#define PIC2_DATA 0xA1

static char master_pic, slave_pic;

/*
 * Diabled PIC beofre use the processor Local APIC and IOAPIC.
 * Interrupt 8/9 (not exception) triggered by PIC when enabling
 * on QEMU platform, interrupt is triggered by PIC.
 */
static void disable_pic(void) {
    /* save PIC value */
    master_pic = inp(PIC1_DATA);
    slave_pic = inp(PIC2_DATA);

    /* disable all IRQs */
    outp(PIC1_DATA, 0xff);
    outp(PIC2_DATA, 0xff);
}

void restore_pic(void)
{
    outp(PIC1_DATA, master_pic);
    outp(PIC2_DATA, slave_pic);
}

LK_INIT_HOOK_FLAGS(restore_pic, (lk_init_hook)restore_pic,
        LK_INIT_LEVEL_LAST-1, LK_INIT_FLAG_PRIMARY_CPU);

void platform_init_interrupts(void)
{
    /*
     * Mask all interrputs before LK bootup
     */
    disable_pic();
    x86_set_cr8(0xF);
}

status_t mask_interrupt(unsigned int vector)
{
    if (vector >= INT_VECTORS)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

void platform_mask_irqs(void)
{
}

status_t unmask_interrupt(unsigned int vector)
{
    if (vector >= INT_VECTORS)
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

extern enum handler_return sm_handle_irq(void);

enum handler_return platform_irq(x86_iframe_t *frame)
{
    /* get the current vector */
    unsigned int vector = frame->vector;

    THREAD_STATS_INC(interrupts);

    /* deliver the interrupt */
    enum handler_return ret = INT_NO_RESCHEDULE;

    /* EOI should be issued by ISR */
    if (int_handler_table[vector].handler) {
        ret = int_handler_table[vector].
        handler(int_handler_table[vector].arg);
    } else {
        lapic_eoi();
        send_self_ipi(vector);
        /*
         * CAUTION: smc to non-secure world, and will not
         * return unless Android call smc to secure world
         */
        ret = sm_handle_irq();
    }

    return ret;
}

void register_int_handler(unsigned int vector, int_handler handler, void *arg)
{
    if (vector >= INT_VECTORS)
        panic("register_int_handler: vector out of range %d\n", vector);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    int_handler_table[vector].arg = arg;
    int_handler_table[vector].handler = handler;

    spin_unlock_irqrestore(&lock, state);
}

/*
 * Stubs to resolve compilations errors.  These and other
 * such functions implemented under flags like SM_LIB need
 * to be implemented
 */
status_t sm_intc_fiq_enter(void)
{
    return 0;
}

void sm_intc_fiq_exit(void)
{
}

long smc_intc_get_next_irq(smc32_args_t *args)
{
    long vector;

    for (vector = args->params[0]; vector<INT_VECTORS; vector++)
    {
        if (int_handler_table[vector].handler && (INT_RESCH != vector))
            return vector;
    }
    return -1;
}

long smc_intc_request_fiq(smc32_args_t *args)
{
    return NO_ERROR;
}
