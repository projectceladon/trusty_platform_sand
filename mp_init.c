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
#include <err.h>
#include <arch/x86/mmu.h>
#include <platform.h>
#include <platform/sand.h>
#include <platform/multiboot.h>
#include <arch/x86.h>
#include <arch/x86/mmu.h>
#include <arch/mmu.h>
#include <arch/local_apic.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <kernel/vm.h>
#include "mem_map.h"
#include <lib/trusty/trusty_device_info.h>

#include <printf.h>

#define ONE_MB_ADDR    0x100000


/* Symbols defined at external/lk/arch/x86/64/start16.S */
extern uint64_t ap_entry_16;
extern uint64_t ap_entry_end;
extern uint64_t ap_entry_32;
extern uint64_t ap_gdtr;
extern uint64_t ap_gdt_table;
extern uint64_t ap_gdt_table_end;

extern trusty_startup_info_t g_trusty_startup_info;
#define REAL_MODE_STARTUP       1
#define GDTR_OFFSET_IN_CODE     7
#define AP_ENTRY_32_IN_CODE     0x17
#define REAL_ADDRESS_IN_CODE    0x1E
#define GDTR_OFFSET_IN_PAGE     ((uint64_t)&ap_gdtr- (uint64_t)&ap_entry_16)
#define GDTR_LIMIT              ((uint64_t)&ap_gdt_table_end - (uint64_t)&ap_gdt_table -1ULL)
#define GDT_OFFSET_IN_PAGE      ((uint64_t)&ap_gdt_table - (uint64_t)&ap_entry_16)
#define AP_ENTRY_32_OFFSET      ((uint64_t)&ap_entry_32 - (uint64_t)&ap_entry_16)

inline static bool is_ap_wkup_addrr_available(uint32_t addr)
{
    if (addr < ONE_MB_ADDR)
        return true;
    else
        return false;
}

#define TSC_PER_MS 2000000
static void wait_us(uint64_t us)
{
    uint64_t end_tsc;
    uint64_t val;
    uint32_t low, high;

    rdtsc(low, high);
    val = (uint64_t)((uint64_t)high <<32 | (uint64_t)low);

    end_tsc = val + (us * TSC_PER_MS / 1000ULL);

    while (val < end_tsc)
    {
        __asm__ __volatile__("pause");
        rdtsc(low, high);
        val = (uint64_t)((uint64_t)high <<32 | (uint64_t)low);
    }
}


extern uintptr_t real_run_addr;
/*
 * GDTR layout in start16.S
 *
 *****************************************************
 0                       // uint16_t 0
 0                       // uint16_t limit
 0                       // uint32_t base
 0x0ULL                  // uint64_t 0x08 (useless)
 0x00cf9a000000ffffULL   // uint64_t 0x10 (CS)
 0x00cf93000000ffffULL   // uint64_t 0x18 (DS)
 *****************************************************
 */

/*
 * Update ap_entry_16 assembly code in start16.S.
 * More comments has been provided in start16.S.
 * You need to objdump start16.o to find out offset
 * if you want to understand this snippet of code.
 *
 *      OPCODE              OFFSET  INSTRUCTION
 *==============================================================
 * 0xB8, PTCH, PTCH,            0x00:   mov REAL_MODE_STARTUP, %ax
 * 0x8E, 0xD8,                  0x03:   mov %ax, %ds
 * 0x8D, 0x36, PTCH, PTCH,      0x05:   lea GDTR_OFFSET_IN_PAGE, %si
 * 0x0F, 0x01, 0x14,            0x09:   lgdt fword ptr [si]
 * 0x0F, 0x20, 0xC0,            0x0C:   mov %cr0, %eax
 * 0x83, 0xC8, 0x01             0x0F:   or $1, %al
 * 0x0F, 0x22, 0xC0,            0x12:   mov %eax, %cr0
 * 0x66,                        0x15:   prefix
 * 0xEA,                        0x16:   absolute farjump
 * 0xFF, 0xFF, 0x00, 0x00       0x17:   AP_ENTRY_32_OFFSET
 * 0x08, 0x00                   0x1B:   code segment
 * 0xBD, 0x00, 0x70, 0x09, 00   0x1E:   mov REAL_ADDRESS_IN_CODE, %ebp
 */
static void startup_hotpatch(void *va, uint32_t pa)
{
    // update assembly lange about cs/gdt
    *((uint16_t *)(va + REAL_MODE_STARTUP)) = (uint16_t)(pa>>4);
    // no segment change, just use offset in page to fill in GDTR
    *((uint16_t *)(va + GDTR_OFFSET_IN_CODE)) = (uint16_t)(GDTR_OFFSET_IN_PAGE);

    // update GDTR info
    // update limit
    *((uint16_t *)(va + GDTR_OFFSET_IN_PAGE)) = (uint16_t)(GDTR_LIMIT);
    // update base
    *((uint32_t *)(va + GDTR_OFFSET_IN_PAGE + 2)) = (uint32_t)(pa + GDT_OFFSET_IN_PAGE);

    // Update AP entry / EIP/CS
    *((uint32_t *)(va + AP_ENTRY_32_IN_CODE)) = (uint32_t)(pa + AP_ENTRY_32_OFFSET);
    *((uint16_t *)(va + AP_ENTRY_32_IN_CODE + 4)) = (uint16_t)(0x8);

    *((uint32_t *)(va + REAL_ADDRESS_IN_CODE)) = (uint32_t)(real_run_addr - KERNEL_LOAD_OFFSET);
}

extern int cpu_waken_up;
void x86_mp_init(uint32_t ap_startup_addr)
{
    status_t ret;
    uint32_t size;
    arch_flags_t access = ARCH_MMU_FLAG_UNCACHED;
    struct map_range range;
    map_addr_t pml4 = (map_addr_t)paddr_to_kvaddr(get_kernel_cr3());

    if (!is_ap_wkup_addrr_available(ap_startup_addr)) {
        panic("AP startup address inavailable\n");
        return;
    }

    range.start_paddr = (map_addr_t)ap_startup_addr;
    range.start_vaddr = (map_addr_t)(0xFFFFFFFF00000000ULL + ap_startup_addr);
    range.size        = PAGE_SIZE;

    ret = x86_mmu_map_range(pml4, &range, access);
    if (NO_ERROR != ret) {
        dprintf(CRITICAL, "Failed to map SIPI page!\n");
        return;
    }

    size = (uint32_t)((uint64_t)&ap_gdt_table_end - (uint64_t)&ap_entry_16);

    if (size > range.size) {
        dprintf(CRITICAL, "Range check for SIPI page failed!\n");
        return;
    }

    memcpy((void *)range.start_vaddr, &ap_entry_16, size);

    startup_hotpatch((void* )range.start_vaddr, ap_startup_addr);

    broadcast_startup(ap_startup_addr >> 12);

    while (SMP_MAX_CPUS != cpu_waken_up) {
        __asm__ __volatile__("pause":::"memory");
    }

    dprintf(INFO, "All processors(%d) boot up now!\n", cpu_waken_up);

    ret = x86_mmu_unmap(pml4, range.start_vaddr, 1);
    if(NO_ERROR != ret) {
        dprintf(CRITICAL, "Failed to unmap SIPI page!\n");
        return;
    }
}
