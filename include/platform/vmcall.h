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
#ifndef __VMCALL_H
#define __VMCALL_H

#include <lib/sm.h>

void make_smc_vmcall_evmm(smc32_args_t *args, long ret);
void make_smc_vmcall_acrn(smc32_args_t *args, long ret);
extern void (*make_smc_vmcall)(smc32_args_t *, long);

#ifdef EPT_DEBUG
typedef enum {
    ADD,
    REMOVE,
}ept_update_t;

void make_ept_update_vmcall(ept_update_t action, uint64_t start, uint64_t size);
#endif

#endif
