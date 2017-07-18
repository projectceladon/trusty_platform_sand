LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

CPU := generic

SMP_MAX_CPUS ?= 1

ifneq ($(SMP_MAX_CPUS),1)
WITH_SMP := 1
endif

TRUSTY_RSV_MEM_SIZE = 0x01400000
TARGET_MAX_MEM_SIZE = 0x80000000

MODULE_DEPS += \
	lib/cbuf \
	lib/lwip \

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

GLOBAL_INCLUDES += \
	$(LOCAL_DIR)/include

MODULE_SRCS += \
	$(LOCAL_DIR)/interrupts.c \
	$(LOCAL_DIR)/platform.c \
	$(LOCAL_DIR)/timer.c \
	$(LOCAL_DIR)/debug.c \
	$(LOCAL_DIR)/entry.c \
	$(LOCAL_DIR)/vmcall.c \
	$(LOCAL_DIR)/lib/pci/pci_config.c

ifeq ($(WITH_SMP),1)
MODULE_SRCS += \
	$(LOCAL_DIR)/mp.c \
	$(LOCAL_DIR)/mp_init.c
endif



EXTRA_LINKER_SCRIPTS += \
	$(LOCAL_DIR)/user_stack.ld

include make/module.mk

