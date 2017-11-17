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
#include <arch/x86/mmu.h>
#include <platform.h>
#include <platform/sand.h>
#include <platform/multiboot.h>
#include <arch/x86.h>
#include <arch/mmu.h>
#include <arch/local_apic.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <kernel/vm.h>
#include "mem_map.h"
#include <lib/trusty/trusty_device_info.h>

extern int _start;
extern int _end;
extern uint64_t __code_start;
extern uint64_t __code_end;
extern uint64_t __rodata_start;
extern uint64_t __rodata_end;
extern uint64_t __data_start;
extern uint64_t __data_end;
extern uint64_t __bss_start;
extern uint64_t __bss_end;
extern void arch_mmu_init_percpu(void);
#if PRINT_USE_MMIO
extern void init_uart(void);
#endif

trusty_startup_info_t g_trusty_startup_info __ALIGNED(8);
uint8_t g_dev_info_buf[PAGE_SIZE] __ALIGNED(8);
trusty_device_info_t *g_dev_info = (trusty_device_info_t *)g_dev_info_buf;
uintptr_t real_run_addr;

volatile map_addr_t g_cr3 = 0;

#ifdef WITH_KERNEL_VM
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* 16MB of memory mapped where the kernel lives */
    {
        .phys = MEMBASE + KERNEL_LOAD_OFFSET,
        .virt = KERNEL_BASE + KERNEL_LOAD_OFFSET,
        .size = 16*MB,
        .flags = 0,
        .name = "kernel"
    },
    {0}
};

static pmm_arena_t heap_arena = {
    .name = "memory",
    .base = MEMBASE,
    .size = 0, /* default amount of memory in case we don't have multiboot */
    .priority = 1,
    .flags = PMM_ARENA_FLAG_KMAP
};

static void heap_arena_init(void)
{
    heap_arena.base = PAGE_ALIGN(mmu_initial_mappings[0].phys);
    heap_arena.size = PAGE_ALIGN(mmu_initial_mappings[0].size);
}
#endif

static void platform_update_pagetable(void)
{
    struct map_range range;
    arch_flags_t access;
    map_addr_t pml4_table = (map_addr_t)paddr_to_kvaddr(get_kernel_cr3());

    /* kernel code section mapping */
    access = ARCH_MMU_FLAG_PERM_RO;
    range.start_vaddr = (map_addr_t) & __code_start;
    range.start_paddr = (uint64_t)vaddr_to_paddr((void *) & __code_start);
    range.size =
        ((map_addr_t) & __code_end) - ((map_addr_t) & __code_start);
    x86_mmu_map_range(pml4_table, &range, access);

    /* kernel data section mapping */
    access = 0;
#if defined(ARCH_X86_64) || defined(PAE_MODE_ENABLED)
    access |= ARCH_MMU_FLAG_PERM_NO_EXECUTE;
#endif
    range.start_vaddr = (map_addr_t) & __data_start;
    range.start_paddr = (uint64_t)vaddr_to_paddr((void *) & __data_start);
    range.size =
        ((map_addr_t) & __data_end) - ((map_addr_t) & __data_start);
    x86_mmu_map_range(pml4_table, &range, access);

    /* kernel rodata section mapping */
    access = ARCH_MMU_FLAG_PERM_RO;
#if defined(ARCH_X86_64) || defined(PAE_MODE_ENABLED)
    access |= ARCH_MMU_FLAG_PERM_NO_EXECUTE;
#endif
    range.start_vaddr = (map_addr_t) & __rodata_start;
    range.start_paddr = (uint64_t)vaddr_to_paddr((void *) & __rodata_start);
    range.size =
        ((map_addr_t) & __rodata_end) - ((map_addr_t) & __rodata_start);
    x86_mmu_map_range(pml4_table, &range, access);

    /* kernel bss section and kernel heap mappings */
    access = 0;
#ifdef ARCH_X86_64
    access |= ARCH_MMU_FLAG_PERM_NO_EXECUTE;
#endif
    range.start_vaddr =  (map_addr_t) & __bss_start;
    range.start_paddr = (uint64_t)vaddr_to_paddr((void *) & __bss_start);
    range.size = ((map_addr_t) &__bss_end) - ((map_addr_t) & __bss_start);
    x86_mmu_map_range(pml4_table, &range, access);

    /* Mapping lower boundary to kernel start */
    access = ARCH_MMU_FLAG_PERM_NO_EXECUTE;
    range.start_vaddr = (map_addr_t)paddr_to_kvaddr(mmu_initial_mappings[0].phys);
    range.start_paddr = mmu_initial_mappings[0].phys;
    range.size = vaddr_to_paddr((void *)&_start) - mmu_initial_mappings[0].phys;
    x86_mmu_map_range(pml4_table, &range, access | ARCH_MMU_FLAG_NS);

    /* Mapping upper boundary to target maxium memory size */
    map_addr_t va = (map_addr_t)&_end;
    range.start_vaddr = (map_addr_t)PAGE_ALIGN(va);
    range.start_paddr = (uint64_t)vaddr_to_paddr((void *)PAGE_ALIGN(va));
    range.size = ((map_addr_t)(mmu_initial_mappings[0].phys + mmu_initial_mappings[0].size) - range.start_paddr);
    x86_mmu_map_range(pml4_table, &range, access | ARCH_MMU_FLAG_NS);
}

void platform_init_mmu_mappings(void)
{
    /* Moving to the new CR3 */
    g_cr3 = x86_get_cr3();
    x86_set_cr3(g_cr3);
}

void clear_sensitive_data(void)
{
    if(g_trusty_startup_info.size_of_this_struct > 0) {
        if(g_dev_info->size > 0)
            memset(g_dev_info, 0, g_dev_info->size);

        /* clear the g_trusty_startup_info */
        memset(&g_trusty_startup_info, 0, sizeof(trusty_startup_info_t));
    }
}

/*
* TODO: need to enhance the panic handler
* currently, if we got panic in boot stage, the behavior
* is not expect, it will failed with SMC call since Android
* not started yet.
*/
static void platform_heap_init(void)
{
    mmu_initial_mappings[0].phys = g_trusty_startup_info.trusty_mem_base;
    mmu_initial_mappings[0].virt = (vaddr_t)&_start;
    mmu_initial_mappings[0].virt -= (real_run_addr - g_trusty_startup_info.trusty_mem_base);
}

void platform_early_init(void)
{
    /* initialize the heap */
    platform_heap_init();

    /* initialize the interrupt controller */
    platform_init_interrupts();

    /* initialize the timer */
    platform_init_timer();

#ifdef WITH_KERNEL_VM
    heap_arena_init();
    pmm_add_arena(&heap_arena);
#endif

#if PRINT_USE_MMIO
    init_uart();
#endif

#if defined(WITH_SMP) || defined(ISSUE_EOI)
    local_apic_init();
#endif
}

void platform_init(void)
{
    /* MMU init for x86 Archs done after the heap is setup */
   // arch_mmu_init_percpu();

    cse_init();

    platform_init_mmu_mappings();
    x86_mmu_init();

#if WITH_SMP
    x86_mp_init(g_trusty_startup_info.sipi_ap_wkup_addr);
#endif
    platform_update_pagetable();
}
