/*
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
 */

#include <asm.h>
#include "trusty_syscalls_x86.h"

.macro  PROLOG
    pushfq
    pushq %rbp
    pushq %rbx
    pushq %r15
.endm

.macro  MOV_PARAMS
   /* Return Addr and stack*/
   leaq 1f(%rip), %rbx
   movq %rsp, %rbp
.endm

.macro EPILOG
1:
    popq %r15
    popq %rbx
    popq %rbp
    popfq
.endm

FUNCTION(get_device_info)
    PROLOG
    movq $__NR_get_device_info, %rax
    MOV_PARAMS
    sysenter
    EPILOG
    ret
