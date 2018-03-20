/*
 * Copyright (c) 2017, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <debug.h>
#include <string.h>
#include <kernel/vm.h>

#include "cse_msg.h"
#include "heci_impl.h"

#define LOCAL_DEBUG 0
#define DEBUG(fmt, ...) do { if(LOCAL_DEBUG) dprintf(0, "%04u: " fmt, __LINE__, ##__VA_ARGS__); } while(0)

#define CPMS                19200
#define HPET_BASE_ADDRESS   0xFED00000
#define ClockCycles()       *((volatile uint32_t *)(hpet_mmio_base_va + 0xf0))

static uint64_t cse_mmio_base_va;
static uint64_t hpet_mmio_base_va;

static void init_timer(void)
{
    uint32_t reg;
    /* Clear HPET Timer 0 Lower and Upper Comparator Value. */
    *((uint32_t *)(hpet_mmio_base_va + 0x108)) = 0;
    *((uint32_t *)(hpet_mmio_base_va + 0x10c)) = 0;

    /* Enable HPET main counter. */
    reg = *((uint32_t *)(hpet_mmio_base_va + 0x10));
    *((uint32_t *)(hpet_mmio_base_va + 0x10c)) = reg | 1;
}

static int wait_event(uint32_t timeout, int (*fun)(uint64_t), uint64_t arg)
{
    init_timer();
    uint32_t t0 = ClockCycles();
    uint32_t elapsed;
    int res;

    timeout *= CPMS;
    for (;;) {
        if (fun != NULL) {
            res = fun(arg);
            if (res > 0)
                return res;
        }

        if (!timeout)
            continue;

        elapsed = ClockCycles() - t0;
        if (elapsed >= timeout)
            return -1;
    }

    return 0;
}

static uint8_t heci_pci_read8(uint32_t reg)
{
    return *((uint8_t *)(cse_mmio_base_va + reg));
}

static uint32_t heci_pci_read32(uint32_t reg)
{
    return *((uint32_t *)(cse_mmio_base_va + reg));
}

static void heci_pci_write32(uint32_t reg, uint32_t val)
{
    *((uint32_t *)(cse_mmio_base_va + reg)) = val;
}

static void heci_pci_set16(uint32_t reg, uint32_t val)
{
    uint16_t v;

    v = *((uint16_t *)(cse_mmio_base_va + reg));
    *((uint16_t *)(cse_mmio_base_va + reg)) = v | val;
}

static uint32_t heci_reg_read(uint64_t base, uint32_t offset)
{
    return *((uint32_t *)(base + offset));
}

static void heci_reg_write(uint64_t base, uint32_t offset, uint32_t val)
{
    *((uint32_t *)(base + offset)) = val;
}

static int is_dev_ready(uint64_t base)
{
    DEV_CTRL_REG reg;

    reg.data = heci_reg_read(base, SEC_CSR_HA);
    if (reg.bit.SEC_RDY_HRA == 0)
        return 0;

    return 1;
}

/* wait for dev ready */
static int is_dev_data_ready(uint64_t base)
{
    DEV_CTRL_REG reg;

    reg.data = heci_reg_read(base, SEC_CSR_HA);
    if (reg.bit.SEC_CBRP_HRA == reg.bit.SEC_CBWP_HRA)
        return 0;

    return 1;
}

static int is_interrupt_raised(uint64_t base)
{
    HOST_CTRL_REG reg;

    reg.data = heci_reg_read(base, H_CSR);
    if (reg.bit.H_IS == 0)
        return 0;

    return 1;
}

/*
 * Verify that the HECI cmd and MBAR regs in its PCI cfg space are setup
 * properly and that the local mHeciContext variable matches this info.
 */
static uint64_t heci_get_base_addr(void)
{
    uint32_t u32HeciBase;
    uint32_t val;
    uint32_t mask;
    uint64_t HeciMBAR_va;
    int status;

    /* Read HECI_MBAR in case it has changed */
    val = heci_pci_read32(HECI_MBAR0) & 0xFFFFFFF0;

    heci_pci_write32(HECI_MBAR0, 0xFFFFFFFF);
    u32HeciBase = heci_pci_read32(HECI_MBAR0) & 0xFFFFFFF0;
    heci_pci_write32(HECI_MBAR0, val);
    u32HeciBase = heci_pci_read32(HECI_MBAR0) & 0xFFFFFFF0;
    DEBUG("HeciMemBase=%x\n", u32HeciBase);

    /* Check if HECI_MBAR is disabled */
    mask = (EFI_PCI_COMMAND_MEMORY_SPACE | EFI_PCI_COMMAND_BUS_MASTER);
    if ((heci_pci_read8(PCI_COMMAND_OFFSET) & mask) != mask)
        heci_pci_set16(PCI_COMMAND_OFFSET, mask | EFI_PCI_COMMAND_SERR);

    status = vmm_alloc_physical(vmm_get_kernel_aspace(), "clock", 4096,
        (void **)&HeciMBAR_va, PAGE_SIZE_SHIFT, (uint64_t)u32HeciBase, 0,
        ARCH_MMU_FLAG_UNCACHED_DEVICE);
    if (status)    {
        dprintf(0, "%s: failed %d\n", __func__, status);
        return 0;
    }
#ifdef EPT_DEBUG
    if (!status)
        make_ept_update_vmcall(ADD, u32HeciBase, 4096);
#endif

    return HeciMBAR_va;
}

/*
 * Send HECI message implement
 */
static int heci_send_impl(uint64_t HeciBase, uint32_t *Message, uint32_t Length, uint8_t HostAddress, uint8_t DevAddr)
{
    uint32_t LeftSize;
    uint32_t MaxBuffer;
    uint32_t WriteSize;
    uint32_t Size;
    uint32_t Index;
    HECI_MSG_HDR head;
    HOST_CTRL_REG hcr;

    if (Message == NULL || Length == 0)
        return 1;

    DEBUG("heci_send Start\n");
    hcr.data = heci_reg_read(HeciBase, H_CSR);
    MaxBuffer = hcr.bit.H_CBD;

    /* The first DWORD used for send MessageHeader, so useable Buffer Size
     * should be MaxBuffer -1;
     */
    MaxBuffer -= 1;
    LeftSize = (Length + 3)/4;
    WriteSize = 0;
    hcr.bit.H_RDY = 1;
    heci_reg_write(HeciBase, H_CSR, hcr.data);
    while (LeftSize > 0) {
        DEBUG("Wait DEV, CSR %x\n", heci_reg_read(HeciBase, SEC_CSR_HA));

        if (wait_event(HECI_EVENT_TIMEOUT, is_dev_ready, HeciBase) < 0) {
            DEBUG("Timeout waiting heci device\n");
            return 1;
        }

        hcr.data = heci_reg_read(HeciBase, H_CSR);
        hcr.bit.H_RDY = 1;
        hcr.bit.H_IE = 0;
        heci_reg_write(HeciBase, H_CSR, hcr.data);
        Size = (LeftSize > MaxBuffer) ? MaxBuffer : LeftSize;

        LeftSize -= Size;

        /* Prepare message header */
        head.data = 0;
        head.bit.sec_address = DevAddr;
        head.bit.host_address = HostAddress;
        head.bit.message_complete = (LeftSize > 0) ? 0 : 1;
        if (LeftSize > 0)
            head.bit.length = Size * sizeof(uint32_t);
        else
            head.bit.length = Length - WriteSize * sizeof(uint32_t);

        DEBUG("heci Message Header: %08x\n", head.data);
        heci_reg_write(HeciBase, H_CB_WW, head.data);
        for (Index = 0; Index < Size; Index++) {
            DEBUG("heci data[%x] = %08x\n", Index, Message[Index + WriteSize]);
            heci_reg_write(HeciBase, H_CB_WW, Message[Index + WriteSize]);
        }

        /* Send the Interrupt; */
        hcr.data = heci_reg_read(HeciBase, H_CSR);
        hcr.bit.H_IS = 1;
        hcr.bit.H_RDY = 1;
        hcr.bit.H_IE = 0;
        hcr.bit.H_IG = 1;
        heci_reg_write(HeciBase, H_CSR, hcr.data);

        WriteSize += Size;
        if (LeftSize > 0) {
            DEBUG("HostControlReg %x\n", heci_reg_read(HeciBase, SEC_CSR_HA));

            if (wait_event(HECI_EVENT_TIMEOUT, is_interrupt_raised, HeciBase)) {
                DEBUG("Timeout waiting interrupt\n");
                return 1;
            }
        }
    }
    DEBUG("heci_send End\n");
    return 0;
}

static int heci_send(uint32_t *Message, uint32_t Length, uint8_t HostAddress, uint8_t DevAddr)
{
    int ret;
    uint64_t HeciBase;

    HeciBase = heci_get_base_addr();
    if (HeciBase == 0)
        return 1;

    ret = heci_send_impl(HeciBase, Message, Length, HostAddress, DevAddr);

    return ret;
}

/*
 * Receive HECI message
 */
static int heci_receive_impl(uint64_t HeciBase, uint32_t *Message, uint32_t *Length)
{
    uint32_t ReadSize = 0;
    uint32_t Index;
    uint32_t BufSize = 0;
    uint32_t value;
    HECI_MSG_HDR head;

    HOST_CTRL_REG hcr;

    DEBUG("heci_receive Start\n");

    if (Length != NULL)
        BufSize = *Length;
    while (1) {
        hcr.data = heci_reg_read(HeciBase, H_CSR);
        hcr.bit.H_RDY = 1;
        hcr.bit.H_IE = 0;
        heci_reg_write(HeciBase, H_CSR, hcr.data);
        DEBUG("Disable Interrupt, HCR: %08x\n", heci_reg_read(HeciBase, H_CSR));

        if (wait_event(HECI_EVENT_TIMEOUT, is_dev_data_ready, HeciBase) < 0)
            goto rx_failed;

        head.data = heci_reg_read(HeciBase, SEC_CB_RW);
        DEBUG("Get Message Header: %08x\n", head.data);
        for (Index = 0; Index < (uint32_t)((head.bit.length + 3)/4); Index++) {
            if (wait_event(HECI_EVENT_TIMEOUT, is_dev_data_ready, HeciBase) < 0)
                goto rx_failed;

            value = heci_reg_read(HeciBase, SEC_CB_RW);
            DEBUG("heci data[%x] = %08x\n", Index, value);
            if (Message != NULL && (BufSize == 0 || BufSize >= (uint32_t)(Index * 4)))
                Message[Index + ReadSize] = value;
        }

        if (Length != NULL) {
            if ((uint32_t)(Index * 4) > head.bit.length)
                *Length = head.bit.length;
            else
                *Length = (uint32_t)(Index * 4);
        }

        hcr.data = heci_reg_read(HeciBase, H_CSR);
        hcr.bit.H_IS = 1;
        hcr.bit.H_RDY = 1;
        hcr.bit.H_IE = 0;
        hcr.bit.H_IG = 1;
        heci_reg_write(HeciBase, H_CSR, hcr.data);
        if (head.bit.message_complete == 1)
            break;
    }
    DEBUG("heci_receive End\n");
    return 0;

rx_failed:
    DEBUG("Timeout during data recv\n");
    return 1;
}

static int heci_receive(uint32_t *Message, uint32_t *Length)
{
    int ret;

    uint64_t HeciBase;

    HeciBase = heci_get_base_addr();
    if (HeciBase == 0)
        return 1;

    ret = heci_receive_impl(HeciBase, Message, Length);
    return ret;
}

static int HeciSendwACK(
        uint32_t           *Message,
        uint32_t           Length,
        uint32_t           *RecLength,
        uint8_t            HostAddress,
        uint8_t            SECAddress
        )
{
    int status = 0;
    uint64_t HeciBase;

    HeciBase = heci_get_base_addr();
    if (HeciBase == 0)
        return 1;

    /* Send the message */
    status = heci_send_impl(HeciBase, Message, Length, HostAddress, SECAddress);
    if (status)
        return status;

    /* Wait for ACK message */
    status = heci_receive_impl(HeciBase, Message, RecLength);
    if (status)
        return status;

    return status;
}

/* if get failure, return zero */
uint32_t get_attkb(uint8_t *attkb)
{
    int status = 0;
    uint32_t HeciSendLength;
    uint32_t HeciRecvLength;
    MCA_BOOTLOADER_READ_ATTKB_RESP_DATA *Resp;
    MCA_BOOTLOADER_READ_ATTKB_REQ_DATA *Req;
    uint8_t DataBuffer[sizeof(MCA_BOOTLOADER_READ_ATTKB_REQ_DATA)];
    paddr_t pa;

    pa = (uint64_t)vaddr_to_paddr(attkb);

    memset(DataBuffer, 0, sizeof(DataBuffer));
    Req = (MCA_BOOTLOADER_READ_ATTKB_REQ_DATA*)DataBuffer;
    Req->MKHIHeader.Fields.GroupId = MCA_MKHI_BOOTLOADER_READ_ATTKB_GRP_ID;
    Req->MKHIHeader.Fields.Command = MCA_MKHI_BOOTLOADER_READ_ATTKB_CMD_REQ;
    Req->DstAddrLower = (uint32_t)pa;
    Req->DstAddrUpper = (uint32_t)(pa >> 32);
    Req->Size = MAX_ATTKB_SIZE;

    HeciSendLength = sizeof(MCA_BOOTLOADER_READ_ATTKB_REQ_DATA);
    HeciRecvLength = sizeof(DataBuffer);

    status = HeciSendwACK(
                 (uint32_t *)DataBuffer,
                 HeciSendLength,
                 &HeciRecvLength,
                 BIOS_FIXED_HOST_ADDR,
                 BIOS_SEC_ADDR);
    Resp = (MCA_BOOTLOADER_READ_ATTKB_RESP_DATA*)DataBuffer;
    if((status < 0) || (Resp->Header.Fields.Result != 0)) {
        dprintf(INFO, "failed to get attkb: status %d, respone %d\n", status, Resp->Header.Fields.Result);
        return 0;
    }
    DEBUG ("Group    = %08x\n", Resp->Header.Fields.GroupId);
    DEBUG ("Command  = %08x\n", Resp->Header.Fields.Command);
    DEBUG ("IsRespone= %08x\n", Resp->Header.Fields.IsResponse);
    DEBUG ("Result   = %08x\n", Resp->Header.Fields.Result);
    DEBUG ("ReadSize = %08x\n", Resp->ReadSize);

    return Resp->ReadSize;
}

void cse_init(void)
{
    status_t ret;

    ret = vmm_alloc_physical(vmm_get_kernel_aspace(), "cse", 4096,
        (void **)&cse_mmio_base_va, PAGE_SIZE_SHIFT,
        (uint64_t)PCI_BDF(HECI_BUS, HECI_DEVICE_NUMBER, HECI_FUNCTION_NUMBER),
        0, ARCH_MMU_FLAG_UNCACHED_DEVICE);
    if (ret)    {
        dprintf(CRITICAL, "%s: failed %d\n", __func__, ret);
        return;
    }
#ifdef EPT_DEBUG
    if (!ret)
        make_ept_update_vmcall(ADD,
        (uint64_t)PCI_BDF(HECI_BUS, HECI_DEVICE_NUMBER, HECI_FUNCTION_NUMBER), 4096);
#endif

    ret = vmm_alloc_physical(vmm_get_kernel_aspace(), "clock", 4096,
        (void **)&hpet_mmio_base_va, PAGE_SIZE_SHIFT, HPET_BASE_ADDRESS, 0,
        ARCH_MMU_FLAG_UNCACHED_DEVICE);
    if (ret)    {
        dprintf(CRITICAL, "%s: failed %d\n", __func__, ret);
        return;
    }
#ifdef EPT_DEBUG
    if (!ret)
        make_ept_update_vmcall(ADD, HPET_BASE_ADDRESS, 4096);
#endif

    return;
}

