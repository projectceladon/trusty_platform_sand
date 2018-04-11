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

#include <stdio.h>
#include <lib/sm.h>
#include <lib/sm/sm_err.h>
#include <arch/local_apic.h>
#include <platform/sand_defs.h>
#include <lk/init.h>
#include <debug.h>

/* Max entity defined as SMC_NUM_ENTITIES(64) */
#define SMC_ENTITY_SMC_X86 63 /* Used for customized SMC calls */
#define SMC_SC_LK_TIMER SMC_STDCALL_NR(SMC_ENTITY_SMC_X86, 0)

static long self_ipi_trigger_timer_intr(void)
{
    /*
     * Send self IPI needs Local APIC support.
     * Since issue EOI also needs Local APIC support, share same
     * macro on send self IPI and issue EOI.
     */
#if ISSUE_EOI
    send_self_ipi(INT_PIT);
#else
    __asm__ __volatile__ ("int $0x31");
#endif

    return 0;
}

static long smc_x86_stdcall(smc32_args_t *args)
{
    switch (args->smc_nr) {
        case SMC_SC_LK_TIMER:
            return self_ipi_trigger_timer_intr();
        default:
            return SM_ERR_UNDEFINED_SMC;
    }

    return 0;
}

static smc32_entity_t smc_x86_entity= {
    .stdcall_handler = smc_x86_stdcall,
};

static void smc_x86_init(uint level)
{
    int err;

    err = sm_register_entity(SMC_ENTITY_SMC_X86, &smc_x86_entity);
    if (err) {
        dprintf(CRITICAL,"Failed to register self IPI: %d\n", err);
    }
}
LK_INIT_HOOK(x86smc, smc_x86_init, LK_INIT_LEVEL_APPS);

long smc_intc_fiq_resume(smc32_args_t *args)
{
    return 0;
}
