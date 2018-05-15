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
#include <macros.h>
#include <arch/defines.h>

#include <platform/vmcall.h>

#define EVMM_SMC_HC_ID                  0x74727500
#define ACRN_SMC_HC_ID                  0x80000071

#ifdef EPT_DEBUG
#define EVMM_EPT_UPDATE_HC_ID           0x65707501
#endif

/* The SMC was called before smc_init() on Simics, then LK will crash.
 * Workaround: initialize make_smc_vmcall with make_smc_vmcall_evmm(). */
void (*make_smc_vmcall)(smc32_args_t *args, long ret) = make_smc_vmcall_evmm;

void make_smc_vmcall_evmm(smc32_args_t *args, long ret)
{
    register unsigned long smc_id __asm__("rax") = EVMM_SMC_HC_ID;

    __asm__ __volatile__(
        "vmcall;"
        :"=D"(args->smc_nr), "=S"(args->params[0]),
        "=d"(args->params[1]), "=b"(args->params[2])
        :"r"(smc_id), "D"(ret), "S"(args->params[0]),
        "d"(args->params[1]), "b"(args->params[2])
    );
}

void make_smc_vmcall_acrn(smc32_args_t *args, long ret)
{
    register unsigned long smc_id __asm__("r8") = ACRN_SMC_HC_ID;

    __asm__ __volatile__(
        "vmcall;"
        :"=D"(args->smc_nr), "=S"(args->params[0]),
        "=d"(args->params[1]), "=b"(args->params[2])
        :"r"(smc_id), "D"(ret), "S"(args->params[0]),
        "d"(args->params[1]), "b"(args->params[2])
        :"rax"
    );
}

#ifdef EPT_DEBUG
void make_ept_update_vmcall(ept_update_t action, uint64_t start, uint64_t size)
{
    uint64_t start_align = ROUNDDOWN(start, PAGE_SIZE);
    uint64_t end_align = ROUNDUP(start+size, PAGE_SIZE);
    size = end_align - start_align;
    int flush_all_cpus = 0;

#if SMP_MAX_CPUS > 1
    flush_all_cpus = 1;
#endif

    __asm__ __volatile__(
        "vmcall;"
        :
        :"a"(EVMM_EPT_UPDATE_HC_ID), "D"(start), "S"(size),
        "d"(action), "c"(flush_all_cpus)
    );
}
#endif
