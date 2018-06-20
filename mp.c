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
#include <assert.h>
#include <trace.h>
#include <err.h>
#include <arch/mp.h>
#include <arch/ops.h>
#include <arch/local_apic.h>
#include <kernel/mp.h>
#include <platform/interrupts.h>
#include <platform/sand.h>
#include <platform/vmcall.h>

static void send_reschedule_ipi(uint32_t cpuid)
{
    /*
     * There should not any IPI at run time,
     * since only one processor runs on run time due to
     * Google Trusty SMC mechanism.
     */
    if (!is_lk_boot_complete()) {
        if (UINT32_MAX == cpuid) {
            return lapic_send_ipi_excluding_self(APIC_DM_FIXED, INT_RESCH);
        } else {
            uint32_t lapic_id = get_lapic_id(cpuid);

            return lapic_send_ipi_to_cpu(lapic_id, APIC_DM_FIXED, INT_RESCH);
        }
    }

    return true;
}

status_t arch_mp_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi)
{
    if (MP_CPU_ALL_BUT_LOCAL == target) {
        send_reschedule_ipi(MP_CPU_ALL_BUT_LOCAL);
    } else {
        uint32_t cpu_id = 0;

        for(cpu_id = 0; cpu_id < SMP_MAX_CPUS; cpu_id++) {
            if(BIT_GET(target, cpu_id))
                send_reschedule_ipi(cpu_id);
        }
    }

    return NO_ERROR;
}

enum handler_return x86_ipi_generic_handler(void *arg)
{

    return INT_NO_RESCHEDULE;
}

enum handler_return x86_ipi_reschedule_handler(void *arg)
{
    enum handler_return ret = INT_RESCHEDULE;

    lapic_eoi();

    /* If LK boots complete, this vector should be redirected to REE */
    if (is_lk_boot_complete()) {
        send_self_ipi(INT_RESCH);
        ret = sm_handle_irq();
    }

    return ret;
}

void arch_mp_init_percpu(void)
{
    lapic_id_init();

    register_int_handler(INT_RESCH, &x86_ipi_reschedule_handler, NULL);
}
