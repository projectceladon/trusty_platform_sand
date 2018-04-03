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
#ifndef __SAND_H
#define __SAND_H

#include <stdint.h>
#include <stdbool.h>
#include <platform/sand_defs.h>
#include <platform/uart.h>
#include <platform/pci_config.h>
#include "trusty_device_info.h"

/* Please keep align with definition in iKGT trusty_guest.h*/
typedef struct _trusty_startup_info{
    /* Used to double check structures match */
    uint32_t size_of_this_struct;

    /* IMR size for LK */
    uint32_t mem_size;

    /* Used to calibrate TSC in LK */
    uint64_t calibrate_tsc_per_ms;

    /* Used by keymaster */
    uint64_t trusty_mem_base;

    uint32_t sipi_ap_wkup_addr;
    uint8_t  padding[4];
}trusty_startup_info_t;

extern trusty_startup_info_t g_trusty_startup_info;
extern device_sec_info_t* g_sec_info;

void platform_init_interrupts(void);
void platform_init_timer(void);
void platform_init_uart(void);
void clear_sensitive_data(void);
bool is_lk_boot_complete(void);

uint8_t pci_read8(uint8_t bus,
            uint8_t device,
            uint8_t function,
            uint8_t reg_id);

void pci_write8(uint8_t bus,
            uint8_t device,
            uint8_t function,
            uint8_t reg_id,
            uint8_t value);

uint16_t pci_read16(uint8_t bus,
            uint8_t device,
            uint8_t function,
            uint8_t reg_id);

void pci_write16(uint8_t bus,
            uint8_t device,
            uint8_t function,
            uint8_t reg_id,
            uint16_t value);

uint32_t pci_read32(uint8_t bus,
            uint8_t device,
            uint8_t function,
            uint8_t reg_id);

void pci_write32(uint8_t bus,
             uint8_t device,
             uint8_t function,
             uint8_t reg_id,
             uint32_t value);

uint64_t pci_read_bar0(uint16_t pci_location);

#if WITH_SMP
void x86_mp_init(uint32_t ap_startup_addr);
#endif

void cse_init(void);
uint32_t get_attkb(uint8_t *attkb);
static inline void x86_set_cr8(uint64_t in_val)
{
       __asm__ __volatile__ (
               "movq %0, %%cr8 \n\t"
               :
               :"r" (in_val));
}
#endif
