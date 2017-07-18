/*******************************************************************************
 * Copyright (c) 2016 Intel Corporation
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
#ifndef __SAND_DEFS_H
#define __SAND_DEFS_H

/* NOTE: keep arch/x86/crt0.S or crt0_64.S in sync with these definitions */

/* interrupts */
#define INT_VECTORS 0xFF

/* defined interrupts */
#define INT_PIT             0x31 /* 0x31 is not used in Android */
#define INT_RESCH           0xF7

/* exceptions */
#define INT_DIVIDE_0        0x00
#define INT_DEBUG_EX        0x01
#define INT_INVALID_OP      0x06
#define INT_DEV_NA_EX       0x07
#define INT_MF              0x10
#define INT_XM              0x13

/* faults */
#define INT_STACK_FAULT     0x0c
#define INT_GP_FAULT        0x0d
#define INT_PAGE_FAULT      0x0e

#define KILOBYTE    *1024
#define KILOBYTES   KILOBYTE
#define MEGABYTE    *1024 KILOBYTES
#define MEGABYTES   MEGABYTE
#define GIGABYTE    *1024 MEGABYTES
#define GIGABYTES   GIGABYTE

#define PACKED  __attribute((packed))

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/* compile time alignment */
#define MASK64_LOW(n) ((1ULL<<(n)) - 1)
#define MASK64_MID(h, l) ((1ULL << ((h)+1)) - (1ULL << (l)))
#define MAKE64(high, low) (((uint64_t)(high))<<32 | (((uint64_t)(low)) & MASK64_LOW(32)))

#define ALIGN_N(__type, __var, __alignment) \
	__type __attribute__((aligned(__alignment))) __var
#define NAKED_N(__type) (__attribute__ ((naked)) __type)

#define ALIGN64(__type, __var) ALIGN_N(__type, __var, 64)
#define ALIGN16(__type, __var) ALIGN_N(__type, __var, 16)
#define ALIGN8(__type, __var)  ALIGN_N(__type, __var, 8)

#define UINT64_ALL_ONES ((uint64_t)-1)
#define UINT32_ALL_ONES ((uint32_t)-1)
#define UINT16_ALL_ONES ((uint16_t)-1)
#define UINT8_ALL_ONES  ((uint8_t)-1)
#define SIZE_T_ALL_ONES  ((size_t)-1)
#define INVALID_PHYSICAL_ADDRESS ((uint64_t)-1)

#define BITMAP_SET(__word, __mask) ((__word) |= (__mask))
#define BITMAP_CLR(__word, __mask) ((__word) &= ~(__mask))
#define BITMAP_GET(__word, __mask) ((__word) & (__mask))
#define BITMAP_ASSIGN(__word, __mask, __value) {    \
            BITMAP_CLR(__word, __mask);     \
            __word |= BITMAP_GET(__value, __mask);  \
}

#define BITMAP_SET64(__word, __mask) ((__word) |= (uint64_t)(__mask))
#define BITMAP_CLR64(__word, __mask) ((__word) &= ~(uint64_t)(__mask))
#define BITMAP_GET64(__word, __mask) ((__word) & (uint64_t)(__mask))
#define BITMAP_ASSIGN64(__word, __mask, __value) {  \
            BITMAP_CLR64(__word, __mask);   \
            __word |= BITMAP_GET64(__value, __mask);    \
}

#define BIT_VALUE(__bitno) (1 << (__bitno))
#define BIT_SET(__word, __bitno) BITMAP_SET(__word, 1 << (__bitno))
#define BIT_CLR(__word, __bitno) BITMAP_CLR(__word, 1 << (__bitno))
#define BIT_GET(__word, __bitno) (((__word) >> (__bitno)) & 1)

#define BIT_VALUE64(__bitno) ((uint64_t)1 << (__bitno))
#define BIT_SET64(__word, __bitno) BITMAP_SET(__word, (uint64_t)1 << (__bitno))
#define BIT_CLR64(__word, __bitno) BITMAP_CLR(__word, (uint64_t)1 << (__bitno))
#define BIT_GET64(__word, __bitno) BIT_GET(__word, __bitno)

#endif
