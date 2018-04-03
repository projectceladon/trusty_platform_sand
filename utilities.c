/*******************************************************************************
 * Copyright (c) 2018 Intel Corporation
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
#include <arch/x86.h>
#include <lk/init.h>
#include <platform/sand.h>

static bool lk_boot_complete = false;

bool is_lk_boot_complete(void)
{
    return lk_boot_complete;
}

void set_lk_boot_complete(void)
{
    lk_boot_complete = true;

    /*
     * All interrupts are masked at LK boot stage,
     * unmask all interruptes before LK switch back to NS world.
     */
    x86_set_cr8(0);

#if WITH_SMP
    /*
     * Local APIC is disabled by default, if LK enabled Local APIC
     * without disabled it, Linux kernel will detected this exception.
     * Since Linux kernel believes Local APIC is disabled by default.
     */
    lapic_software_disable();
#endif
}

/*
 * Design as x86 architecutre:
 *  1. proxy timer needs Android support
 *  2. SMP boot protocol needs AP switches back to Android first
 *  and stays in WATI_FOR_SIPI status
 * needs to check whether BSP boots complete, and prepare swtich back to Android.
 */
LK_INIT_HOOK_FLAGS(set_lk_boot_status, (lk_init_hook) set_lk_boot_complete,
        LK_INIT_LEVEL_LAST, LK_INIT_FLAG_PRIMARY_CPU);

