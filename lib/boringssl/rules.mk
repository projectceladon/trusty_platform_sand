# Copyright (c) 2015 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# This file is not used in the Android build process! It's used only by Trusty.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ifeq (true, $(call TOBOOL,$(ENABLE_STATIC_LIB)))
MODULE_STATIC_LIB := true
endif

BORINGSSL_ROOT := $(LOCAL_DIR)/../../../../../external/boringssl

ifeq ($(ARCH), x86)
TARGET_ARCH := $(SUBARCH)
TARGET_2ND_ARCH := $(SUBARCH)
else
TARGET_ARCH := $(ARCH)
TARGET_2ND_ARCH := $(ARCH)
endif

# Reset local variables
LOCAL_CFLAGS :=
LOCAL_C_INCLUDES :=
LOCAL_SRC_FILES_$(TARGET_ARCH) :=
LOCAL_SRC_FILES_$(TARGET_2ND_ARCH) :=
LOCAL_CFLAGS_$(TARGET_ARCH) :=
LOCAL_CFLAGS_$(TARGET_2ND_ARCH) :=
LOCAL_ADDITIONAL_DEPENDENCIES :=

# get target_c_flags, target_c_includes, target_src_files
MODULE_SRCDEPS += $(LOCAL_DIR)/crypto-sources.mk
include $(LOCAL_DIR)/crypto-sources.mk

# Some files in BoringSSL use OS functions that aren't supported by Trusty. The
# easiest way to deal with them is not to include them. As long as no path to
# the functions defined in these files exists, the linker will be happy. If
# such a path is created, it'll be a link-time error and something more complex
# may need to be considered.
LOCAL_SRC_FILES := $(filter-out android_compat_hacks.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/asn1/time_support.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/bio/connect.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/bio/fd.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/bio/file.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/bio/socket.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/bio/socket_helper.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/directory_posix.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/rand/urandom.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/time_support.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/x509/by_dir.c,$(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/crypto/x509v3/v3_pci.c,$(LOCAL_SRC_FILES))

# BoringSSL detects Trusty based on this define and does things like switch to
# no-op threading functions.
MODULE_CFLAGS += -DTRUSTY -DANDROID
MODULE_CFLAGS += -fvisibility=hidden -DBORINGSSL_SHARED_LIBRARY -DBORINGSSL_IMPLEMENTATION -Wno-unused-parameter \
	-m64 -march=slm -mfpmath=sse  -DUSE_SSSE3 -mssse3 -msse4 -msse4.1 -msse4.2 -maes

# BoringSSL expects an STL to be available when building for C++11 to provide
# scopers. Suppress those APIs.
GLOBAL_CPPFLAGS += -DBORINGSSL_NO_CXX

MODULE_SRCS += $(addprefix $(BORINGSSL_ROOT)/,$(LOCAL_SRC_FILES))
ifeq ($(ARCH), x86)
MODULE_SRCS += $(addprefix $(BORINGSSL_ROOT)/,$(LOCAL_SRC_FILES_$(SUBARCH)))
else
MODULE_SRCS += $(addprefix $(BORINGSSL_ROOT)/,$(LOCAL_SRC_FILES_$(ARCH)))
endif
MODULE_SRCS += $(addprefix $(LOCAL_DIR)/,$(LOCAL_SRC_FILES_OVERRIDE))

LOCAL_C_INCLUDES := src/crypto src/include

GLOBAL_INCLUDES += $(addprefix $(BORINGSSL_ROOT)/,$(LOCAL_C_INCLUDES))

MODULE_DEPS := \
	lib/openssl-stubs \
	lib/libc-trusty

include make/module.mk
