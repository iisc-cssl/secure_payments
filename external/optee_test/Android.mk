LOCAL_PATH := $(call my-dir)

## include variants like TA_DEV_KIT_DIR
## and OPTEE_BIN
INCLUDE_FOR_BUILD_TA := false
include $(BUILD_OPTEE_MK)
INCLUDE_FOR_BUILD_TA :=

VERSION = $(shell git describe --always --dirty=-dev 2>/dev/null || echo Unknown)

# TA_DEV_KIT_DIR must be set to non-empty value to
# avoid the Android build scripts complaining about
# includes pointing outside the Android source tree.
# This var is expected to be set when OPTEE OS built.
# We set the default value to an invalid path.
TA_DEV_KIT_DIR ?= ../invalid_include_path

-include $(TA_DEV_KIT_DIR)/host_include/conf.mk

include $(CLEAR_VARS)
LOCAL_MODULE := xtest
LOCAL_VENDOR_MODULE := true
LOCAL_SHARED_LIBRARIES := libteec

TA_DIR ?= /vendor/lib/optee_armtz

srcs := regression_1000.c

ifeq ($(CFG_GP_SOCKETS),y)
srcs += regression_2000.c \
	sock_server.c \
	rand_stream.c
endif

srcs +=	adbg/src/adbg_case.c \
	adbg/src/adbg_enum.c \
	adbg/src/adbg_expect.c \
	adbg/src/adbg_log.c \
	adbg/src/adbg_run.c \
	adbg/src/security_utils_hex.c \
	aes_perf.c \
	pio_perf.c \
	benchmark_1000.c \
	benchmark_2000.c \
	regression_4000.c \
	regression_4100.c \
	regression_5000.c \
	regression_6000.c \
	regression_7000.c \
	regression_8000.c \
	regression_8100.c \
	tui_tests.c \
	sha_perf.c \
	xtest_helpers.c \
	xtest_main.c \
	xtest_test.c

ifeq ($(CFG_SECSTOR_TA_MGMT_PTA),y)
srcs += install_ta.c
endif

ifeq ($(CFG_SECURE_DATA_PATH),y)
srcs += sdp_basic.c
endif

define my-embed-file
$(TARGET_OUT_HEADERS)/$(1).h: $(LOCAL_PATH)/$(2)
	@echo '  GEN     $$@'
	@$(LOCAL_PATH)/scripts/file_to_c.py --inf $$< --out $$@ --name $(1)

LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_HEADERS)/$(1).h

endef

$(eval $(call my-embed-file,regression_8100_ca_crt,cert/ca.crt))
$(eval $(call my-embed-file,regression_8100_mid_crt,cert/mid.crt))
$(eval $(call my-embed-file,regression_8100_my_crt,cert/my.crt))
$(eval $(call my-embed-file,regression_8100_my_csr,cert/my.csr))

LOCAL_SRC_FILES := $(patsubst %,host/xtest/%,$(srcs))

LOCAL_C_INCLUDES += $(LOCAL_PATH)/host/xtest \
		$(LOCAL_PATH)/host/xtest/adbg/include\
		$(LOCAL_PATH)/host/xtest/xml/include \
		$(LOCAL_PATH)/ta/concurrent/include \
		$(LOCAL_PATH)/ta/concurrent_large/include \
		$(LOCAL_PATH)/ta/create_fail_test/include \
		$(LOCAL_PATH)/ta/crypt/include \
		$(LOCAL_PATH)/ta/enc_fs/include \
		$(LOCAL_PATH)/ta/os_test/include \
		$(LOCAL_PATH)/ta/rpc_test/include \
		$(LOCAL_PATH)/ta/pio_test/include \
		$(LOCAL_PATH)/ta/sims/include \
		$(LOCAL_PATH)/ta/include \
		$(LOCAL_PATH)/ta/storage_benchmark/include \
		$(LOCAL_PATH)/ta/sha_perf/include \
		$(LOCAL_PATH)/ta/aes_perf/include \
		$(LOCAL_PATH)/ta/socket/include \
		$(LOCAL_PATH)/ta/sdp_basic/include

# Include configuration file generated by OP-TEE OS (CFG_* macros)
LOCAL_CFLAGS += -I $(TA_DEV_KIT_DIR)/host_include -include conf.h
LOCAL_CFLAGS += -pthread
LOCAL_CFLAGS += -g3
LOCAL_CFLAGS += -Wno-missing-field-initializers -Wno-format-zero-length

ifneq ($(TA_DIR),)
LOCAL_CFLAGS += -DTA_DIR=\"$(TA_DIR)\"
endif

## $(OPTEE_BIN) is the path of tee.bin like
## out/target/product/hikey/optee/arm-plat-hikey/core/tee.bin
## it will be generated after build the optee_os with target BUILD_OPTEE_OS
## which is defined in the common ta build mk file included before,
LOCAL_ADDITIONAL_DEPENDENCIES += $(OPTEE_BIN)

include $(BUILD_EXECUTABLE)

include $(LOCAL_PATH)/ta/Android.mk
