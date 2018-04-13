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
#include <string.h>
#include <assert.h>
#include <arch/x86/mmu.h>
#include <arch/x86.h>
#include <arch/local_apic.h>
#include <kernel/vm.h>
#include <platform/sand.h>
#include <platform/vmcall.h>

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

/* Store physical address LK is located. */
uintptr_t entry_phys = 0;

/* For 16MB memory mapping */
map_addr_t pde_kernel[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);
/* Acutally needs 8 entries only, 1 more for unalignment mapping */
map_addr_t pte_kernel[NO_OF_PT_ENTRIES * 9] __ALIGNED(PAGE_SIZE);
trusty_startup_info_t g_trusty_startup_info __ALIGNED(8);
uint8_t g_sec_info_buf[PAGE_SIZE] __ALIGNED(8);
device_sec_info_t *g_sec_info = (device_sec_info_t *)g_sec_info_buf;

enum {
    VMM_ID_EVMM = 0,
    VMM_ID_ACRN,
    VMM_SUPPORTED_NUM
} vmm_id_t;

static const char *vmm_signature[] = {
    [VMM_ID_EVMM] = "EVMMEVMMEVMM",
    [VMM_ID_ACRN] = "ACRNACRNACRN"
};

static inline int detect_vmm(void)
{
    uint32_t signature[3];
    int i;

    __asm__ __volatile__ (
        "xchgl %%ebx, %0  \n\t"  // save ebx
        "cpuid            \n\t"
        "xchgl %%ebx, %0  \n\t"  // restore ebx
        : "=r" (signature[0]),
          "=c" (signature[1]),
          "=d" (signature[2])
        : "a" (0x40000000)
        : "cc");

    for (i=0; i<VMM_SUPPORTED_NUM; i++) {
        if (!memcmp(vmm_signature[i], signature, 12))
            return i;
    }
    return -1;
}

#ifdef WITH_KERNEL_VM
#if TRUSTY_ANDROID_P
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* 16MB of memory mapped where the kernel lives */
    {
        .phys = MEMBASE + KERNEL_LOAD_OFFSET,
        .virt = KERNEL_BASE + KERNEL_LOAD_OFFSET,
        .size = 16*MB,
        .flags = MMU_INITIAL_MAPPING_TEMPORARY,
        .name = "kernel"
    },
    {
        .phys = MEMBASE + KERNEL_LOAD_OFFSET,
        .virt = KERNEL_ASPACE_BASE + KERNEL_LOAD_OFFSET,
        .size = 64*GB,
        .flags = 0,
        .name = "memory"
    },
    {0}
};
#else
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
#endif

static pmm_arena_t heap_arena = {
    .name = "memory",
    .base = MEMBASE,
    .size = 0, /* default amount of memory in case we don't have multiboot */
    .priority = 1,
    .flags = PMM_ARENA_FLAG_KMAP
};

static void heap_arena_init(void)
{
#if TRUSTY_ANDROID_P
    uint64_t rsvd = (uint64_t)&__bss_end - (uint64_t)(mmu_initial_mappings[0].virt);
    rsvd += KERNEL_LOAD_OFFSET;
#else
    uint64_t rsvd = 0;
#endif

    heap_arena.base = PAGE_ALIGN(mmu_initial_mappings[0].phys + rsvd);
    heap_arena.size = PAGE_ALIGN(mmu_initial_mappings[0].size - rsvd);
}
#endif

static void platform_update_pagetable(void)
{
    struct map_range range;
    arch_flags_t access;
    map_addr_t pml4_table = (map_addr_t)paddr_to_kvaddr(x86_get_cr3());

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
    /* Flush TLB */
    x86_set_cr3(x86_get_cr3());
}

void clear_sensitive_data(void)
{
    if(g_trusty_startup_info.size_of_this_struct > 0) {
        if(g_sec_info->size_of_this_struct > 0)
            memset(g_sec_info, 0, g_sec_info->size_of_this_struct);

        /* clear the g_trusty_startup_info */
        memset(&g_trusty_startup_info, 0, sizeof(trusty_startup_info_t));
    }
}

void smc_init(void)
{
    int vmm_id;

    vmm_id = detect_vmm();
    if (vmm_id == VMM_ID_EVMM) {
        make_smc_vmcall = make_smc_vmcall_evmm;
    } else if (vmm_id == VMM_ID_ACRN) {
        make_smc_vmcall = make_smc_vmcall_acrn;
    } else {
        dprintf(CRITICAL, "Trusty is not yet supported on Current VMM!\n");
        ASSERT(0);
    }

    dprintf(INFO, "Detected VMM: signature=%s\n", vmm_signature[vmm_id]);
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
    mmu_initial_mappings[0].virt -= (entry_phys - g_trusty_startup_info.trusty_mem_base);

    mmu_initial_mappings[1].phys = g_trusty_startup_info.trusty_mem_base;
    mmu_initial_mappings[1].virt = g_trusty_startup_info.trusty_mem_base + KERNEL_ASPACE_BASE;
    mmu_initial_mappings[1].size -= g_trusty_startup_info.trusty_mem_base;
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

    smc_init();

#if ATTKB_HECI
    cse_init();
#endif

    platform_init_mmu_mappings();
    x86_mmu_init();

#if WITH_SMP
    x86_mp_init(g_trusty_startup_info.sipi_ap_wkup_addr);
#endif
    platform_update_pagetable();
}
