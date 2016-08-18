# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := acpisvc

MODULE_TYPE := userapp

MODULE_CFLAGS += -Wno-strict-aliasing -Ithird_party/lib/acpica/source/include

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c

MODULE_STATIC_LIBS := \
    ulib/acpica \
    ulib/runtime \
    ulib/ddk \

MODULE_LIBS := \
    ulib/launchpad \
    ulib/magenta \
    ulib/musl \
    ulib/mxio \

include make/module.mk
