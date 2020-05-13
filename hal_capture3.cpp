#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

#define LOG_TAG "[[[HALCAPUTRE]]]"

#include <utils/Log.h>
#include <common/CameraProviderManager.h>
#include <common/CameraDeviceBase.h>
#include <device3/Camera3Device.h>
#include <gui/IGraphicBufferProducer.h>
#include <gui/BufferQueue.h>
#include <gui/CpuConsumer.h>
#include <binder/MemoryHeapBase.h>
#include <utils/Condition.h>
#include <camera/CameraMetadata.h>
#include <api1/client2/Parameters.h>

FILE *g_console = stderr;

/* Macros borrowed from capture tool */
#define INFO(...) \
    do { \
        fprintf(g_console, __VA_ARGS__); \
        fprintf(g_console, "\n"); \
        ALOGI(__VA_ARGS__); \
    } while(0)

#define ERROR(...) \
    do { \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        ALOGE(__VA_ARGS__); \
    } while(0)

using namespace android;

sp<CameraProviderManager> gCameraProviderManager;
int g_num_of_cams = 0;
int g_camera_id = 0;

/* Utility Functions */
Condition gExitSignal;
Mutex gExitMutex;

class HALCaptureCallback : public CameraDeviceBase::NotificationListener {
public:
    void notifyIdle() {
        gExitSignal.signal();
        return;
    }
    void notifyShutter(const CaptureResultExtras &resultExtras,
                nsecs_t timestamp) {
        return;
    }
    void notifyPrepared(int streamId) {
        return;
    }
    void notifyRequestQueueEmpty() {
        return;
    }
    void notifyError(int32_t errorCode,
                const CaptureResultExtras &resultExtras) {
        return;
    }
    void notifyAutoFocus(uint8_t newState, int triggerId) {
        return;
    }
    void notifyAutoExposure(uint8_t newState, int triggerId) {
        return;
    }
    void notifyAutoWhitebalance(uint8_t newState,
                int triggerId) {
        return;
    }
    void notifyRepeatingRequestError(long lastFrameNumber) {
        return;
    }
};

sp<HALCaptureCallback> g_HALCaptureCallback;

sp<CpuConsumer> gCaptureConsumer;
sp<Surface> gCaptureWindow;
CameraMetadata gDeviceInfo;
ssize_t maxJpegSize = 0;

class HALFrameListener : public CpuConsumer::FrameAvailableListener {
public:
    
};

class JpegListener : // public Thread, 
        public camera3::Camera3StreamBufferListener, 
        public CpuConsumer::FrameAvailableListener {
public:
    bool mCaptureDone = false;

    status_t processNewCapture() {

        CpuConsumer::LockedBuffer imgBuffer;
        int res = 0;
        gCaptureConsumer->lockNextBuffer(&imgBuffer);
        if (res != OK) {
            if (res != BAD_VALUE) {
                ERROR("%s: Camera %d: Error receiving still image buffer: "
                        "%s (%d)", __FUNCTION__,
                        g_camera_id, strerror(-res), res);
            }
            return res;
        }
        INFO("%s: Camera %d: Still capture available", __FUNCTION__,
                g_camera_id);

        if (imgBuffer.format != HAL_PIXEL_FORMAT_BLOB) {
            ERROR("%s: Camera %d: Unexpected format for still image: "
                    "%x, expected %x", __FUNCTION__, g_camera_id,
                    imgBuffer.format,
                    HAL_PIXEL_FORMAT_BLOB);
            gCaptureConsumer->unlockBuffer(imgBuffer);
            return OK;
        }
        size_t jpegSize = imgBuffer.width;
        INFO("get jpegSize: %d\n", jpegSize);
        FILE *fp = ::fopen("/data/tmp.jpg", "w");
        if(NULL != fp) {
            ::fwrite(imgBuffer.data, 1, jpegSize, fp);
            ::fclose(fp);
        }
        gCaptureConsumer->unlockBuffer(imgBuffer);
        return OK;
    }

    void onBufferAcquired(const BufferInfo& bufferInfo) {
    }
    void onBufferReleased(const BufferInfo& bufferInfo) {
    }
    void onBufferRequestForFrameNumber(uint64_t frameNumber, int streamId,
            const CameraMetadata& settings) {
        return;
    }

    void onFrameAvailable(const BufferItem& item) {
        processNewCapture();
        return;
    }
    void onFrameReplaced(const BufferItem& item) {

        return;
    }
};

sp<JpegListener> g_JpegListener;

int init(void) {
    int rc = -1;

    if(nullptr == gCameraProviderManager.get()) {
        gCameraProviderManager = new CameraProviderManager();
        rc = gCameraProviderManager->initialize(NULL);
        if(android::OK != rc) {
            ERROR("_CameraProviderManager->initialize return error: %d\n", rc);
        }
    }
    if(nullptr == g_HALCaptureCallback.get()) {
        g_HALCaptureCallback = new HALCaptureCallback();
    }
    if(nullptr == g_JpegListener.get()) {
        g_JpegListener = new JpegListener();
    }
    sp<IGraphicBufferProducer> producer;
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&producer, &consumer);

    gCaptureConsumer = new CpuConsumer(consumer, 1);
    gCaptureConsumer->setFrameAvailableListener(g_JpegListener);
    gCaptureConsumer->setName(String8("JpegConsumer"));
    gCaptureWindow = new Surface(producer);

    return rc;
}

int android_camera_get_camera_count() {
    if(gCameraProviderManager.get()) {
        return gCameraProviderManager->getCameraCount();
    }
    return 0;
}

void getMaxJpegResolution(uint32_t *__width, uint32_t *__height) {
    int32_t maxJpegWidth = 0, maxJpegHeight = 0;
    const int STREAM_CONFIGURATION_SIZE = 4;
    const int STREAM_FORMAT_OFFSET = 0;
    const int STREAM_WIDTH_OFFSET = 1;
    const int STREAM_HEIGHT_OFFSET = 2;
    const int STREAM_IS_INPUT_OFFSET = 3;
    camera_metadata_entry availableStreamConfigs =
            gDeviceInfo.find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    if (availableStreamConfigs.count == 0 ||
            availableStreamConfigs.count % STREAM_CONFIGURATION_SIZE != 0) {
        *__width = 0;
        *__height = 0;
        return;
    }

    // Get max jpeg size (area-wise).
    for (size_t i=0; i < availableStreamConfigs.count; i+= STREAM_CONFIGURATION_SIZE) {
        int32_t format = availableStreamConfigs.data.i32[i + STREAM_FORMAT_OFFSET];
        int32_t width = availableStreamConfigs.data.i32[i + STREAM_WIDTH_OFFSET];
        int32_t height = availableStreamConfigs.data.i32[i + STREAM_HEIGHT_OFFSET];
        int32_t isInput = availableStreamConfigs.data.i32[i + STREAM_IS_INPUT_OFFSET];
        if (isInput == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT
                && format == HAL_PIXEL_FORMAT_BLOB &&
                (width * height > maxJpegWidth * maxJpegHeight)) {
            maxJpegWidth = width;
            maxJpegHeight = height;
        }
    }

    *__width = maxJpegWidth;
    *__height = maxJpegHeight;
    return;
}

#define DEFAULT_FACING hardware::CAMERA_FACING_BACK

int main(/* int argc, const char **argv */) {
    std::vector<std::string> normalDeviceIds;
    int res = init();
    int i = 0, camera_count = 0;
    char camera_device_name[10];

    if(android::OK == res) {
        INFO("gCameraProviderManager pointer is: %p\n", gCameraProviderManager.get());
    }
    camera_count = android_camera_get_camera_count();
    if(0 >= camera_count) {
        ERROR("Found no cameras on this system");
        exit(EXIT_FAILURE);
    }
    INFO("camera count: %d\n", camera_count);
    normalDeviceIds =
			gCameraProviderManager->getCameraDeviceIds();

    for(i = 0; i < camera_count; i++) {
        g_camera_id = i;
        CameraInfo info;
        res = gCameraProviderManager->getCameraInfo(normalDeviceIds[i], &info);
		if (res) {
			ERROR("Error in get_camera_info line %d", __LINE__);
			exit(EXIT_FAILURE);
		}
		INFO("facing=%d", info.facing);
		if (DEFAULT_FACING == info.facing)
			break;
    }
    INFO("id: %d\n", g_camera_id);
    snprintf(camera_device_name, sizeof(camera_device_name), "%d",
		 g_camera_id);
    sp<CameraDeviceBase> device = new Camera3Device(String8(camera_device_name));
    device->initialize(gCameraProviderManager, String8(camera_device_name));
    device->setNotifyCallback(g_HALCaptureCallback);

    int mCaptureStreamId;

    uint32_t pictureWidth = 0, pictureHeight = 0;
    res = gCameraProviderManager->getCameraCharacteristics(camera_device_name, &gDeviceInfo);
    if (res != OK) {
        ERROR("Could not retrieve camera characteristics: %s (%d)", strerror(-res), res);
        return res;
    }
    getMaxJpegResolution(&pictureWidth, &pictureHeight);
    INFO("width: %d, height: %d\n", pictureWidth, pictureHeight);
    
    // Find out buffer size for JPEG
    maxJpegSize = device->getJpegBufferSize(pictureWidth, pictureHeight);
    if (maxJpegSize <= 0) {
        ERROR("%s: Camera %d: Jpeg buffer size (%zu) is invalid ",
                __FUNCTION__, g_camera_id, maxJpegSize);
        return INVALID_OPERATION;
    }
    INFO("maxJpegSize: %d\n", maxJpegSize);

    res = device->createStream(gCaptureWindow,
                pictureWidth, pictureHeight,
                HAL_PIXEL_FORMAT_BLOB, HAL_DATASPACE_V0_JFIF,
                CAMERA3_STREAM_ROTATION_0, &mCaptureStreamId,
                String8());
    if (res != OK) {
        ERROR("%s: Camera %d: Can't create output stream for capture: "
                "%s (%d)", __FUNCTION__, g_camera_id,
                strerror(-res), res);
        return res;
    }

    res = device->addBufferListenerForStream(mCaptureStreamId, g_JpegListener);
    if (res != OK) {
            ERROR("%s: Camera %d: Can't add buffer listener: %s (%d)",
                __FUNCTION__, g_camera_id, strerror(-res), res);
            return res;
    }

    CameraMetadata request;
    res = device->createDefaultRequest(
            CAMERA2_TEMPLATE_STILL_CAPTURE,
            &request);
    if (res != OK) {
        ERROR("%s: Camera %d: Unable to create default still image request:"
                " %s (%d)", __FUNCTION__, g_camera_id,
                strerror(-res), res);
        return res;
    }
    Vector<int32_t> outputStreams;
    outputStreams.push(mCaptureStreamId);
    uint8_t captureIntent = static_cast<uint8_t>(ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE);
    res = request.update(ANDROID_CONTROL_CAPTURE_INTENT, &captureIntent, 1);
    if (res != OK) {
        ERROR("%s: Camera %d: Unable to update capture intent: %s (%d)",
                __FUNCTION__, g_camera_id, strerror(-res), res);
        return res;
    }
    
    res = request.update(ANDROID_REQUEST_OUTPUT_STREAMS,
            outputStreams);

    int32_t mCaptureId;
    if (res == OK) {
        res = request.update(ANDROID_REQUEST_ID,
                &mCaptureId, 1);
    }

    camera2::SharedParameters parameters(g_camera_id, DEFAULT_FACING);
    android::camera2::SharedParameters::Lock l(parameters);
    res = l.mParameters.initialize(device.get(), 0);

    CameraMetadata info = device->info();
    res = l.mParameters.updateRequest(&request);
    if (res != OK) {
        ERROR("%s: Camera %d: Unable to update common entries of capture "
                "request: %s (%d)", __FUNCTION__, g_camera_id,
                strerror(-res), res);
        return res;
    }

    res = l.mParameters.updateRequestJpeg(&request);
    if (res != OK) {
        ERROR("%s: Camera %d: Unable to update JPEG entries of capture "
                "request: %s (%d)", __FUNCTION__, g_camera_id,
                strerror(-res), res);
        return res;
    }

    device->flush();
    res = device->waitUntilDrained();
    if (res != OK) {
        ERROR("%s: Camera %d: Waiting device drain failed: %s (%d)",
                __FUNCTION__, g_camera_id, strerror(-res), res);
    }
    device->triggerPrecaptureMetering(0);

    res = device->capture(request);
    if (res != OK) {
        ERROR("%s: Camera %d: Unable to submit still image capture request: "
                "%s (%d)",
                __FUNCTION__, g_camera_id, strerror(-res), res);
        return res;
    }

    const nsecs_t kWaitDuration = 1000000000LL; // 1 s
    res = gExitSignal.waitRelative(gExitMutex,
                    kWaitDuration);
    device->deleteStream(mCaptureStreamId);
    device->disconnect();
    INFO("exit\n");
    return 0;
}
