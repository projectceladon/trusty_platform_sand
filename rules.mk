LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

CPU := generic

SMP_MAX_CPUS ?= 1

ifneq ($(SMP_MAX_CPUS),1)
WITH_SMP := 1
endif

#disable attkb through heci by default
ATTKB_HECI ?= 0
ifeq ($(ATTKB_HECI), 1)
GLOBAL_DEFINES += \
        ATTKB_HECI=1
endif

MODULE_DEPS += \
	lib/cbuf

WITH_SM_WALL=1
GLOBAL_DEFINES += \
	    PLATFORM_HAS_DYNAMIC_TIMER=1

ifneq (,$(RUNTIME_MEM_BASE))
GLOBAL_DEFINES += \
	    RT_MEM_BASE=$(RUNTIME_MEM_BASE)
endif

ifeq (true, $(ENABLE_TRUSTY_SIMICS))
GLOBAL_DEFINES += \
	    ENABLE_TRUSTY_SIMICS=1
endif

ifeq (true, $(WITH_CUSTOMIZED_SYSCALL))
GLOBAL_DEFINES += \
	    WITH_CUSTOMIZED_SYSCALL=1
endif

GLOBAL_INCLUDES += \
	$(LOCAL_DIR)/include

MODULE_SRCS += \
	$(LOCAL_DIR)/interrupts.c \
	$(LOCAL_DIR)/platform.c \
	$(LOCAL_DIR)/timer.c \
	$(LOCAL_DIR)/debug.c \
	$(LOCAL_DIR)/entry.c \
	$(LOCAL_DIR)/vmcall.c \
	$(LOCAL_DIR)/lib/pci/pci_config.c \
	$(LOCAL_DIR)/lib/syscall/syscall_x86.c \
	$(LOCAL_DIR)/lib/smc/smc_x86.c \
	$(LOCAL_DIR)/utilities.c \


ifeq ($(ATTKB_HECI), 1)
MODULE_SRCS += \
	$(LOCAL_DIR)/lib/heci/heci.c
endif

ifeq ($(WITH_SMP),1)
MODULE_SRCS += \
	$(LOCAL_DIR)/mp.c \
	$(LOCAL_DIR)/mp_init.c
endif

ifeq (true, $(WITH_CUSTOMIZED_BOOTSTRAP))
#WITH_CUSTOMIZED_BOOTSTRAP := true
MODULE_SRCS += \
	$(LOCAL_DIR)/start.S
endif

EXTRA_LINKER_SCRIPTS += \
	$(LOCAL_DIR)/user_stack.ld

include make/module.mk

