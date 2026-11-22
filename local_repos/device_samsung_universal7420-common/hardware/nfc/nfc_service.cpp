#define LOG_TAG "NfcAidlExynos"

#include <aidl/android/hardware/nfc/BnNfc.h>
#include <aidl/android/hardware/nfc/INfcClientCallback.h>
#include <aidl/android/hardware/nfc/NfcCloseType.h>
#include <aidl/android/hardware/nfc/NfcConfig.h>
#include <aidl/android/hardware/nfc/NfcEvent.h>
#include <aidl/android/hardware/nfc/NfcStatus.h>
#include <aidl/android/hardware/nfc/PresenceCheckAlgorithm.h>

#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <hardware/nfc.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Samsung halimpl entry points
int nfc_hal_init(void);
void nfc_hal_deinit(void);
int nfc_hal_open(nfc_stack_callback_t* p_cback, nfc_stack_data_callback_t* p_data_cback);
int nfc_hal_write(uint16_t data_len, const uint8_t* p_data);
int nfc_hal_core_initialized(uint8_t* p_core_init_rsp_params);
int nfc_hal_pre_discover(void);
int nfc_hal_close(void);
int nfc_hal_control_granted(void);
int nfc_hal_power_cycle(void);
int nfc_hal_factory_reset(void);
int nfc_hal_closeForPowerOffCase(void);

namespace aidl::android::hardware::nfc {

using ::ndk::ScopedAStatus;

namespace {

std::mutex gCbMutex;
std::mutex gLifecycleMutex;
std::shared_ptr<INfcClientCallback> gCallback;
std::atomic<bool> gHalOpen{false};
std::atomic<bool> gVerbose{false};
uintptr_t gSessionId = 0;
AIBinder* gClientBinder = nullptr;
AIBinder_DeathRecipient* gDeathRecipient = nullptr;

void closeLocked(bool callHalClose) {
    if (gClientBinder) {
        if (gDeathRecipient) {
            AIBinder_unlinkToDeath(gClientBinder, gDeathRecipient, reinterpret_cast<void*>(gSessionId));
        }
        AIBinder_decStrong(gClientBinder);
        gClientBinder = nullptr;
    }
    if (callHalClose && gHalOpen.load()) {
        nfc_hal_close();
        gHalOpen.store(false);
    }
    std::lock_guard<std::mutex> lk(gCbMutex);
    gCallback.reset();
}

void onClientBinderDied(void* cookie) {
    const uintptr_t deadSession = reinterpret_cast<uintptr_t>(cookie);
    std::lock_guard<std::mutex> lk(gLifecycleMutex);
    if (deadSession == gSessionId && gHalOpen.load()) {
        ALOGW("NFC client binder died (session=%zu); closing HAL to release /dev/sec-nfc",
              static_cast<size_t>(deadSession));
        closeLocked(true);
    }
}

NfcEvent mapEvent(nfc_event_t evt) {
    switch (evt) {
        case HAL_NFC_OPEN_CPLT_EVT: return NfcEvent::OPEN_CPLT;
        case HAL_NFC_CLOSE_CPLT_EVT: return NfcEvent::CLOSE_CPLT;
        case HAL_NFC_POST_INIT_CPLT_EVT: return NfcEvent::POST_INIT_CPLT;
        case HAL_NFC_PRE_DISCOVER_CPLT_EVT: return NfcEvent::PRE_DISCOVER_CPLT;
        case HAL_NFC_ERROR_EVT: return NfcEvent::ERROR;
        default: return NfcEvent::ERROR;
    }
}

NfcStatus mapStatus(nfc_status_t st) {
    switch (st) {
        case HAL_NFC_STATUS_OK: return NfcStatus::OK;
        case HAL_NFC_STATUS_FAILED: return NfcStatus::FAILED;
        case HAL_NFC_STATUS_ERR_TRANSPORT: return NfcStatus::ERR_TRANSPORT;
        case HAL_NFC_STATUS_ERR_CMD_TIMEOUT: return NfcStatus::ERR_CMD_TIMEOUT;
        case HAL_NFC_STATUS_REFUSED: return NfcStatus::REFUSED;
        default: return NfcStatus::FAILED;
    }
}

void eventCallback(uint8_t event, uint8_t status) {
    std::shared_ptr<INfcClientCallback> cb;
    {
        std::lock_guard<std::mutex> lk(gCbMutex);
        cb = gCallback;
    }
    ALOGI("eventCallback(evt=%u, status=%u)", event, status);
    if (!cb) return;
    if (event == HAL_NFC_REQUEST_CONTROL_EVT) {
        // AIDL INfc removed requestControl/controlGranted; grant immediately.
        nfc_hal_control_granted();
        return;
    }
    if (event == HAL_NFC_RELEASE_CONTROL_EVT) {
        return;
    }
    auto st = cb->sendEvent(mapEvent(event), mapStatus(status));
    if (!st.isOk()) ALOGW("sendEvent failed: %s", st.getDescription().c_str());
}

void dataCallback(uint16_t len, uint8_t* data) {
    std::shared_ptr<INfcClientCallback> cb;
    {
        std::lock_guard<std::mutex> lk(gCbMutex);
        cb = gCallback;
    }
    if (!cb || !data || len == 0) return;
    std::vector<uint8_t> vec(data, data + len);
    auto st = cb->sendData(vec);
    if (!st.isOk()) ALOGW("sendData failed: %s", st.getDescription().c_str());
}

}  // namespace

class SecNfcAidl : public BnNfc {
public:
    ScopedAStatus open(const std::shared_ptr<INfcClientCallback>& clientCallback) override {
        if (!clientCallback) {
            return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(NfcStatus::FAILED));
        }
        std::lock_guard<std::mutex> lifeLk(gLifecycleMutex);
        if (gHalOpen.load()) {
            ALOGW("open() called while HAL already open; closing previous session first");
            closeLocked(true);
        }
        {
            std::lock_guard<std::mutex> lk(gCbMutex);
            gCallback = clientCallback;
        }
        int rc = nfc_hal_open(eventCallback, dataCallback);
        ALOGI("nfc_hal_open() -> %d", rc);
        if (rc == 0) {
            gHalOpen.store(true);
            ++gSessionId;
            AIBinder* binder = clientCallback->asBinder().get();
            if (binder && gDeathRecipient) {
                AIBinder_incStrong(binder);
                gClientBinder = binder;
                AIBinder_linkToDeath(binder, gDeathRecipient, reinterpret_cast<void*>(gSessionId));
            }
            return ScopedAStatus::ok();
        }
        closeLocked(false);
        return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(NfcStatus::FAILED));
    }

    ScopedAStatus close(NfcCloseType type) override {
        std::lock_guard<std::mutex> lifeLk(gLifecycleMutex);
        {
            std::lock_guard<std::mutex> lk(gCbMutex);
            if (!gCallback && !gHalOpen.load()) {
                return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(NfcStatus::FAILED));
            }
        }
        int rc = gHalOpen.load() ? nfc_hal_close() : 0;
        gHalOpen.store(false);
        closeLocked(false);
        ALOGI("nfc_hal_close(type=%d) -> %d", static_cast<int>(type), rc);
        return rc == 0 ? ScopedAStatus::ok()
                       : ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(NfcStatus::FAILED));
    }

    ScopedAStatus coreInitialized() override {
        uint8_t dummy_rsp[4] = {0x40, 0x01, 0x01, 0x00};
        int rc = nfc_hal_core_initialized(dummy_rsp);
        ALOGI("nfc_hal_core_initialized() -> %d", rc);
        return rc == 0 ? ScopedAStatus::ok()
                       : ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(NfcStatus::FAILED));
    }

    ScopedAStatus factoryReset() override {
        return ScopedAStatus::ok();
    }

    ScopedAStatus getConfig(NfcConfig* out) override {
        if (!out) return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(NfcStatus::FAILED));
        *out = NfcConfig{};
        out->nfaPollBailOutMode = false;
        out->presenceCheckAlgorithm = PresenceCheckAlgorithm::ISO_DEP_NAK;
        out->defaultRoute = 0x00;
        out->defaultIsoDepRoute = 0x00;
        out->defaultOffHostRoute = 0x00;
        out->defaultOffHostRouteFelica = 0x00;
        out->defaultSystemCodeRoute = 0x00;
        out->defaultSystemCodePowerState = 0x00;
        out->offHostESEPipeId = 0x00;
        out->offHostSIMPipeId = 0x00;
        out->maxIsoDepTransceiveLength = 0xFEFF;
        return ScopedAStatus::ok();
    }

    ScopedAStatus powerCycle() override {
        int rc = nfc_hal_power_cycle();
        return rc == 0 ? ScopedAStatus::ok()
                       : ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(NfcStatus::FAILED));
    }

    ScopedAStatus preDiscover() override {
        // Samsung S3FWRN5 has no pre-discovery action; return FAILED so libnfc-nci does not wait for PRE_DISCOVER_CPLT.
        return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(NfcStatus::FAILED));
    }

    ScopedAStatus write(const std::vector<uint8_t>& data, int32_t* outWritten) override {
        if (data.empty()) {
            *outWritten = 0;
            return ScopedAStatus::ok();
        }
        int rc = nfc_hal_write(static_cast<uint16_t>(data.size()), data.data());
        *outWritten = rc;
        return ScopedAStatus::ok();
    }

    ScopedAStatus setEnableVerboseLogging(bool enable) override {
        gVerbose.store(enable);
        return ScopedAStatus::ok();
    }

    ScopedAStatus isVerboseLoggingEnabled(bool* out) override {
        *out = gVerbose.load();
        return ScopedAStatus::ok();
    }
};

}  // namespace aidl::android::hardware::nfc

int main() {
    ALOGI("Starting Exynos7420 AIDL NFC HAL service (S3FWRN5)");
    nfc_hal_init();
    aidl::android::hardware::nfc::gDeathRecipient =
            AIBinder_DeathRecipient_new(aidl::android::hardware::nfc::onClientBinderDied);

    ABinderProcess_setThreadPoolMaxThreadCount(2);
    auto service = ndk::SharedRefBase::make<aidl::android::hardware::nfc::SecNfcAidl>();
    const std::string instance = std::string(aidl::android::hardware::nfc::INfc::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(service->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        ALOGE("AServiceManager_addService(%s) failed: %d", instance.c_str(), status);
        return 1;
    }

    ALOGI("Registered %s successfully", instance.c_str());
    ABinderProcess_joinThreadPool();
    nfc_hal_deinit();
    return 0;
}
