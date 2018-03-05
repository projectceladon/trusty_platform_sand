LOCAL_ADDITIONAL_DEPENDENCIES += $(BORINGSSL_ROOT)/sources.mk
include $(BORINGSSL_ROOT)/sources.mk
include $(LOCAL_DIR)/override.mk

# Do not add in the architecture-specific files if we don't want to build assembly
ifeq ($(LOCAL_IS_HOST_MODULE),true)
LOCAL_SRC_FILES_linux_x86 := $(linux_x86_sources)
LOCAL_SRC_FILES_linux_x86-64 := $(linux_x86_64_sources)
else
LOCAL_SRC_FILES_x86 := $(linux_x86_sources)
LOCAL_SRC_FILES_x86-64 := $(linux_x86_64_sources)
LOCAL_SRC_FILES_arm := $(linux_arm_sources)
LOCAL_SRC_FILES_arm64 := $(linux_aarch64_sources)
endif
LOCAL_SRC_FILES += $(crypto_sources)

LOCAL_SRC_FILES_OVERRIDE += $(override_sources)
