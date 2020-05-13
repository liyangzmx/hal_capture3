LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := hal_capture3
# LOCAL_MODULE_TAGS := userdebug debug
LOCAL_SRC_FILES := hal_capture3.cpp
LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libutils \
	libbinder \
	libhardware \
	libcamera_client \
	libgui \
	libui \
	liblog \
	libcameraservice \
	libcamera_client \
	libfmq \
	android.hardware.camera.common@1.0 \
	android.hardware.camera.provider@2.4 \
	android.hardware.camera.provider@2.5 \
	android.hardware.camera.device@1.0 \
	android.hardware.camera.device@3.2 \
	android.hardware.camera.device@3.3 \
	android.hardware.camera.device@3.4 \
	android.hardware.camera.device@3.5

LOCAL_C_INCLUDES += system/media/private/camera/include \
	frameworks/av/services/camera/libcameraservice/ \
	frameworks/av/services/camera/libcameraservice/device1 \
	system/media/camera/include \
	frameworks/av/include/ \
	frameworks/native/include/ \
	frameworks/native/include/binder/ \
	system/core/include/utils/

ifeq (arm64,$(TARGET_ARCH))
    LOCAL_32_BIT_ONLY = true
endif

LOCAL_CFLAGS += -Wno-unused-parameter

include $(BUILD_EXECUTABLE)
