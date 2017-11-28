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
#include <stdarg.h>
#include <reg.h>
#include <stdio.h>
#include <kernel/thread.h>
#include <arch/x86.h>
#include <arch/x86/mmu.h>
#include <lib/cbuf.h>
#include <platform/interrupts.h>
#include <platform/debug.h>
#include <platform/sand.h>
#include <string.h>
#include <kernel/vm.h>
#include <kernel/mutex.h>
#include <platform/vmcall.h>

#include <arch/spinlock.h>

#define BUF_SIZE 4096
static char uart_early_buf[BUF_SIZE];
static int put_idx = 0, get_idx = 0;


extern int having_print_cb;
uint8_t (*io_get_reg)(uint64_t base_addr, uint32_t reg_id);
void (*io_set_reg)(uint64_t base_addr, uint32_t reg_id, uint8_t val);

#if PRINT_USE_MMIO
static uint64_t mmio_base_addr = 0;

static uint8_t uart_mmio_get_reg(uint64_t base_addr, uint32_t reg_id)
{
    return *(volatile uint8_t *)(base_addr + (uint64_t)reg_id * 4);
}

static void uart_mmio_set_reg(uint64_t base_addr, uint32_t reg_id, uint8_t val)
{
    *(volatile uint8_t *)(base_addr + (uint64_t)reg_id * 4) = val;
}

uint64_t get_mmio_base(void)
{
    return mmio_base_addr;
}
#elif PRINT_USE_IO_PORT
inline uint8_t asm_in8(uint16_t port)
{
    uint8_t val8;

    __asm__ __volatile__ (
            "inb %1, %0"
            : "=a" (val8)
            : "d" (port));
    return val8;
}

inline void asm_out8(uint16_t port, uint8_t val8)
{
    __asm__ __volatile__ (
            "outb %1, %0"
            :
            : "d" (port), "a" (val8));

}

static uint8_t serial_io_get_reg(uint64_t base_addr, uint32_t reg_id)
{
    return asm_in8((uint16_t)base_addr + (uint16_t)reg_id);
}

static void serial_io_set_reg(uint64_t base_addr, uint32_t reg_id, uint8_t val)
{
    asm_out8((uint16_t)base_addr + (uint16_t)reg_id, val);
}
#endif

static void uart_putc(char c)
{
    uart_lsr_t lsr;
    uint64_t io_base;

#if PRINT_USE_MMIO
    io_base = mmio_base_addr;
    io_get_reg = uart_mmio_get_reg;
    io_set_reg = uart_mmio_set_reg;
#elif PRINT_USE_IO_PORT
    io_get_reg = serial_io_get_reg;
    io_set_reg = serial_io_set_reg;
    io_base = TARGET_SERIAL_IO_BASE;
#else
    return;
#endif

    put_idx++;
    put_idx %= BUF_SIZE;
    uart_early_buf[put_idx] = c;
    if (put_idx == get_idx) {
        get_idx++;
        get_idx %= BUF_SIZE;
    }

#if PRINT_USE_MMIO
    if (!mmio_base_addr) {
        return;
    }
#endif

    while (put_idx != get_idx) {
        while (1) {
            lsr.data = io_get_reg(io_base, UART_REGISTER_LSR);
            if (lsr.bits.thre == 1)
                break;
        }
        io_set_reg(io_base, UART_REGISTER_THR, uart_early_buf[get_idx]);
        get_idx++;
        get_idx %= BUF_SIZE;
    }
}

static spin_lock_t dputc_spin_lock = 0;
void platform_dputc(char c)
{
    spin_lock_saved_state_t state;
    spin_lock_save(&dputc_spin_lock, &state, SPIN_LOCK_FLAG_IRQ);
    /* only print the log by uart for boot stage */
    if (!having_print_cb) {
        if (c == '\n')
            uart_putc('\r');
        uart_putc(c);
    }
    spin_unlock_restore(&dputc_spin_lock, state, SPIN_LOCK_FLAG_IRQ);
}

int platform_dgetc(char *c, bool wait)
{
    return 0;
}

#if PRINT_USE_MMIO
void init_uart(void)
{
    uint64_t io_base = 0;
    arch_flags_t access = ARCH_MMU_FLAG_PERM_NO_EXECUTE | ARCH_MMU_FLAG_UNCACHED | ARCH_MMU_FLAG_PERM_USER;
    struct map_range range;
    map_addr_t pml4_table = (map_addr_t)paddr_to_kvaddr(get_kernel_cr3());

    io_base = (uint64_t)(pci_read32(0, 24, 2, 0x10) & ~0xF);

    range.start_vaddr = (map_addr_t)(0xFFFFFFFF00000000ULL + (uint64_t)io_base);
    range.start_paddr = (map_addr_t)io_base;
    range.size        = PAGE_SIZE;
    x86_mmu_map_range(pml4_table, &range, access);

    mmio_base_addr = range.start_vaddr;

#ifdef EPT_DEBUG
    make_ept_update_vmcall(ADD, io_base, 4096);
#endif

    uart_putc('\n');
}
#endif

