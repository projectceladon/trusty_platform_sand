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
#include <platform/sand.h>
#include <platform/vmcall.h>
#include <stdlib.h>

#define TRUSTY_VMCALL_SMC               0x74727500 /* "tru" is 0x747275 */

#ifdef EPT_DEBUG
#define VMCALL_EPT_UPDATE        0x65707501
#endif

void make_smc_vmcall(smc32_args_t *args, long ret)
{
    __asm__ __volatile__(
        "vmcall;"
        :"=D"(args->smc_nr), "=S"(args->params[0]),
        "=d"(args->params[1]), "=b"(args->params[2])
        :"a"(TRUSTY_VMCALL_SMC), "D"(ret), "S"(args->params[0]),
        "d"(args->params[1]), "b"(args->params[2])
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
        :"a"(VMCALL_EPT_UPDATE), "D"(start), "S"(size),
        "d"(action), "c"(flush_all_cpus)
    );
}
#endif
