/*
 * Copyright (c) 2009 Corey Tabaka
 * Copyright (c) 2016 Travis Geiselbrecht
 * Copyright (c) 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <asm.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mmu.h>

/* The magic number for the Multiboot header. */
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002

/* The flags for the Multiboot header. */
#if defined(__ELF__) && 0
#define MULTIBOOT_HEADER_FLAGS 0x00000002
#else
#define MULTIBOOT_HEADER_FLAGS 0x00010002
#endif

/* The magic number passed by a Multiboot-compliant boot loader. */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define MSR_EFER    0xc0000080
#define EFER_LME    0x00000100
#define MSR_PAT     0x277
#define CACHE_MODE  0x70106
#define MSR_GS_BASE 0xC0000101

#define PHYS_LOAD_ADDRESS (MEMBASE + KERNEL_LOAD_OFFSET)
#define PHYS_ADDR_DELTA (KERNEL_BASE + KERNEL_LOAD_OFFSET - PHYS_LOAD_ADDRESS)
#define PHYS(x) ((x) - PHYS_ADDR_DELTA)

#define PGDIR_SHIFT      39
#define PUD_SHIFT        30
#define PMD_SHIFT        21
#define PTD_SHIFT        12
#define PTRS_MASK        (512 - 1)

.macro save_info
    /* save g_trusty_startup_info in local */
    movl %edi, %esi
    lea  PHYS(g_trusty_startup_info)(%ebp), %edi
    movl $32, %ecx
    shrl $2, %ecx
    rep  movsl

    /* clear previous g_trusty_startup_info */
    movl $32, %ecx
    shrl $2, %ecx
2:
    movl $0, (%eax)
    addl $4, %eax
    loop 2b

    /* save g_sec_info in local */
    movl PHYS(g_trusty_startup_info + 16)(%ebp), %esi
    lea  PHYS(g_sec_info_buf)(%ebp), %edi
    movl $4096, %ecx
    shrl $2, %ecx
    rep movsl

    /* clear previous g_sec_info */
    movl PHYS(g_trusty_startup_info + 16)(%ebp), %eax
    movl $4096, %ecx
    shrl $2, %ecx
2:
    movl $0, (%eax)
    addl $4, %eax
    loop 2b
.endm

.macro bootstrap_page_init
    /* Setting the First PML4E with a PDP table reference*/
    lea  PHYS(pdp)(%ebp), %eax
    lea  PHYS(pml4)(%ebp), %edx
    orl  $X86_KERNEL_PD_FLAGS, %eax
    movl %eax, (%edx)

    /* Setting the First PDPTE with a Page table reference*/
    /* 0 - 1G  */
    lea  PHYS(pte)(%ebp), %eax
    lea  PHYS(pdp)(%ebp), %edx
    orl  $X86_KERNEL_PD_FLAGS, %eax
    movl %eax, (%edx)

    /* 1G - 2G  */
    lea  PHYS(pte + 0x1000)(%ebp), %eax
    add  $0x8, %edx
    orl  $X86_KERNEL_PD_FLAGS, %eax
    movl %eax, (%edx)

    /* point the pml4e at the last 512G (kernel aspace 64GB mapping) */
    lea  PHYS(pdp_high)(%ebp), %eax
    lea  PHYS(pml4 + 8*511)(%ebp), %edx
    orl  $X86_KERNEL_PD_FLAGS, %eax
    movl %eax, (%edx)

    /* map the first 2GB in this table */
    lea  PHYS(pte)(%ebp), %esi
    movl $0x400, %ecx
    xor  %eax, %eax

0:
    mov  %eax, %ebx
    shll $21, %ebx
    orl  $X86_KERNEL_PD_LP_FLAGS, %ebx
    movl %ebx, (%esi)
    addl $8,%esi
    inc  %eax
    loop 0b

    /* set up a linear map of the first 64GB from 0xffffff8000000000 */
    lea  PHYS(linear_map_pdp)(%ebp), %esi
    movl $32768, %ecx
    xor  %eax, %eax

    /* loop across these page tables, incrementing the address by 2MB */
0:
    mov  %eax, %ebx
    shll $21, %ebx
    orl  $X86_KERNEL_PD_LP_FLAGS, %ebx    # lower word of the entry
    movl %ebx, (%esi)
    mov  %eax, %ebx
    shrl $11, %ebx      # upper word of the entry
    movl %ebx, 4(%esi)
    addl $8,%esi
    inc  %eax
    loop 0b

    /* point the high pdp at our linear mapping page tables */
    lea  PHYS(pdp_high)(%ebp), %esi
    movl $64, %ecx
    lea  PHYS(linear_map_pdp)(%ebp), %eax
    orl  $X86_KERNEL_PD_FLAGS, %eax

0:
    movl %eax, (%esi)
    add  $8, %esi
    addl $4096, %eax
    loop 0b
.endm

.macro  map_customized_page_table
    /* -2G adress */
    leaq pdp_high + 8*510(%rip), %rdi
    leaq pde_kernel(%rip), %rsi
    orq  $X86_KERNEL_PD_FLAGS, %rsi
    movq %rsi, (%rdi)

    /* Map 8*2M=16M, this is hard code */
    leaq pde_kernel(%rip), %rdi
    leaq pte_kernel(%rip), %rsi
    movq $8, %rcx
0:
    orq  $X86_KERNEL_PD_FLAGS, %rsi
    movq %rsi, (%rdi)
    addq $8, %rdi
    addq $4096, %rsi
    loop 0b

    /* we have set offset (0x1000) align to platform requirement */
    movq g_trusty_startup_info + 16(%rip), %rsi
    shrq $20, %rsi
    shlq $20, %rsi
    leaq pte_kernel(%rip), %rdi
    movq $4096, %rcx
0:
    orq  $103, %rsi
    movq %rsi, (%rdi)
    addq $8, %rdi
    addq $4096, %rsi
    loop 0b
.endm

.section ".text.boot"
.code32
.global _start
_start:
    jmp real_start

.align 8

.type multiboot_header,STT_OBJECT
multiboot_header:
    /* magic */
    .int MULTIBOOT_HEADER_MAGIC
    /* flags */
    .int MULTIBOOT_HEADER_FLAGS
    /* checksum */
    .int -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

#if !defined(__ELF__) || 1
    /* header_addr */
    .int PHYS(multiboot_header)
    /* load_addr */
    .int PHYS(_start)
    /* load_end_addr */
    .int PHYS(__data_end)
    /* bss_end_addr */
    .int PHYS(__bss_end)
    /* entry_addr */
    .int PHYS(real_start)
#endif

real_start:
    cmpl $MULTIBOOT_BOOTLOADER_MAGIC, %eax
    jne  0f
    movl %ebx, PHYS(_multiboot_info)

0:
    /*
     * get the offset between compiled entry address and
     * actually entry address in ebp register temporary
     */
    call 1f
1:
    popl %ebp
    subl $PHYS(1b), %ebp

    /* update and load our new gdt by physical pointer */
    lea  PHYS(_gdtr_phys)(%ebp), %eax
    lea  PHYS(_gdt)(%ebp), %edx
    movl %edx, 2(%eax)
    lgdt (%eax)

    /* load our data selectors */
    movw $DATA_SELECTOR, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %ss
    movw %ax, %gs
    movw %ax, %ss

    /* load initial stack pointer */
    lea PHYS(_kstack + 4096)(%ebp), %esp

    /* We need to jump to our sane 32 bit CS */
    pushl $CODE_SELECTOR
    lea   PHYS(.Lfarjump)(%ebp), %eax
    pushl %eax
    retf

.Lfarjump:

    /* zero the bss section */
bss_setup:
    lea  PHYS(__bss_start)(%ebp), %eax /* starting address of the bss */
    lea  PHYS(__bss_end)(%ebp), %ecx   /* find the length of the bss in bytes */
    subl %eax, %ecx
    shrl $2, %ecx       /* convert to 32 bit words, since the bss is aligned anyway */
2:
    movl $0, (%eax)
    addl $4, %eax
    loop 2b

    movl %edi, %eax

    save_info

paging_setup:
    /* Preparing 64 bit paging, we will use 2MB pages covering 2GB
    for initial bootstrap, this page table will be 1 to 1  */

    /* PAE bit must be enabled  for 64 bit paging*/
    mov  %cr4, %eax
    btsl $(5), %eax
    mov  %eax, %cr4

    /* load the physical pointer to the top level page table */
    lea  PHYS(pml4)(%ebp), %eax
    mov  %eax, %cr3

    /* Long Mode Enabled at this point*/
    movl $MSR_EFER, %ecx
    rdmsr
    orl  $EFER_LME, %eax
    wrmsr

    /* setting PAT MSR */
    movl $MSR_PAT, %ecx

    movl $CACHE_MODE, %eax
    movl $CACHE_MODE, %edx
    wrmsr

    /* map low 2G and 64G for address space */
    bootstrap_page_init

    /* Enabling Paging and from this point we are in 32 bit compatibility mode */
    mov %cr0,  %eax
    btsl $(31), %eax
    mov %eax,  %cr0

    /* Using another long jump to be on 64 bit mode */
    pushl $CODE_64_SELECTOR
    lea   PHYS(farjump64)(%ebp), %eax
    pushl %eax
    retf

.align 8
.code64
farjump64:

    map_customized_page_table

    /* update real entry physical address */
    leaq _start(%rip), %rax
    movq %rax, entry_phys(%rip)

    /* branch to our high address */
    mov  $highaddr, %rax
    jmp  *%rax

highaddr:
    xorq %rax, %rax
    mov  %ax,  %gs

    /* set TR now, since lk_main check cpuid when initialing thread */
    mov  $TSS_SELECTOR, %ax
    ltr  %ax

    /* load the high kernel stack */
    mov  $(_kstack + 4096), %rsp

    /* reload the gdtr */
    lgdt _gdtr

#ifdef STACK_PROTECTOR
    /* setup stack check guard for C call */
    leaq __stack_chk_guard(%rip), %rdi
    call get_rand_64
    subq $0, %rax
    jz   0f
#endif

    /* set up the idt */
    call setup_idt

    /* set up GS base */
    leaq global_states(%rip), %rax

    movq %rax, %rdx
    shr  $32,  %rdx
    movq $MSR_GS_BASE, %rcx
    wrmsr

    xorq %rbp, %rbp
    xorq %rdi, %rdi
    xorq %rsi, %rsi
    xorq %rdx, %rdx
    xorq %rcx, %rcx

    /* call the main module */
    call lk_main

0:                          /* just sit around waiting for interrupts */
    hlt                     /* interrupts will unhalt the processor */
    pause
    jmp 0b                  /* so jump back to halt to conserve power */


.align 8
.code64
.org 0x400
_startup_64:
    call 1f
1:
    pop %rbp
    subl $PHYS(1b), %ebp

    /* zero the bss section */
    lea __bss_start(%rip), %eax /* starting address of the bss */
    lea __bss_end(%rip), %ecx   /* find the length of the bss in bytes */
    subl %eax, %ecx
    shrl $2, %ecx       /* convert to 32 bit words, since the bss is aligned anyway */
2:
    movl $0, (%eax)
    addl $4, %eax
    loop 2b

    mov %edi, %eax

    save_info

    bootstrap_page_init

    map_customized_page_table

    /* save the run addr */
    leaq _start(%rip), %rax
    movq %rax, entry_phys(%rip)

    leaq pml4(%rip), %rax
    movq %rax, %cr3
    /* load our gdtr */
    lgdt _gdtr

    /* long jump to our code selector and the high address */
    push  $CODE_64_SELECTOR
    push  $highaddr
    lretq

.global _start_pa
.set _start_pa, _start - KERNEL_BASE
