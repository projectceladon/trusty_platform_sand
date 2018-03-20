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
#include <lib/sm/sm_err.h>
#include <platform/vmcall.h>

extern smc32_handler_t sm_fastcall_table[SMC_NUM_ENTITIES];
extern uint32_t sm_nr_fastcall_functions;
extern smc32_handler_t sm_fastcall_function_table[];
extern long smc_undefined(smc32_args_t * args);

void sm_sched_nonsecure(long retval, smc32_args_t * args)
{
    uint32_t smc_nr;
    u_int entry_nr;
    smc32_handler_t handler_fn = NULL;
return_sm_err:

    make_smc_vmcall(args, retval);

    smc_nr = args->smc_nr;
    if (SMC_IS_SMC64(smc_nr)) {
        retval = SM_ERR_NOT_SUPPORTED;
        goto return_sm_err;
    }
    if (!SMC_IS_FASTCALL(smc_nr)) {
        return;
    }

    /* for fast call */
    entry_nr = SMC_ENTITY(smc_nr);
    handler_fn = sm_fastcall_table[entry_nr];
    retval = handler_fn(args);
    goto return_sm_err;
}

long smc_fastcall_secure_monitor(smc32_args_t * args)
{
    u_int function = SMC_FUNCTION(args->smc_nr);
    smc32_handler_t handler_fn = NULL;

    if (function < sm_nr_fastcall_functions)
        handler_fn = sm_fastcall_function_table[function];

    if (!handler_fn)
        handler_fn = smc_undefined;

    return handler_fn(args);
}
