#define LOG_TAG "GnssAidlExynos"

#include <aidl/android/hardware/gnss/BnGnss.h>
#include <aidl/android/hardware/gnss/GnssConstellationType.h>
#include <aidl/android/hardware/gnss/GnssLocation.h>
#include <aidl/android/hardware/gnss/IGnssCallback.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <dlfcn.h>
#include <hardware/gps.h>
#include <hardware/hardware.h>
#include <pthread.h>
#include <string.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace aidl::android::hardware::gnss {

using ::ndk::ScopedAStatus;

namespace {

std::mutex gCbMutex;
std::shared_ptr<IGnssCallback> gCallback;
bool gSvEnabled = true;
bool gNmeaEnabled = true;

struct ThreadStartArgs {
    void (*start)(void*);
    void* arg;
};

void* threadTrampoline(void* p) {
    auto* a = static_cast<ThreadStartArgs*>(p);
    auto fn = a->start;
    auto arg = a->arg;
    delete a;
    fn(arg);
    return nullptr;
}

pthread_t createThreadCb(const char* name, void (*start)(void*), void* arg) {
    pthread_t tid = 0;
    auto* a = new ThreadStartArgs{start, arg};
    if (pthread_create(&tid, nullptr, threadTrampoline, a) != 0) {
        ALOGE("pthread_create(%s) failed", name ? name : "gnss");
        delete a;
        return 0;
    }
    return tid;
}

std::shared_ptr<IGnssCallback> getCb() {
    std::lock_guard<std::mutex> lk(gCbMutex);
    return gCallback;
}

void locationCb(GpsLocation* loc) {
    auto cb = getCb();
    if (!cb || !loc) return;

    GnssLocation out{};
    int32_t flags = 0;
    if (loc->flags & GPS_LOCATION_HAS_LAT_LONG) flags |= GnssLocation::HAS_LAT_LONG;
    if (loc->flags & GPS_LOCATION_HAS_ALTITUDE) flags |= GnssLocation::HAS_ALTITUDE;
    if (loc->flags & GPS_LOCATION_HAS_SPEED) flags |= GnssLocation::HAS_SPEED;
    if (loc->flags & GPS_LOCATION_HAS_BEARING) flags |= GnssLocation::HAS_BEARING;
    if (loc->flags & GPS_LOCATION_HAS_ACCURACY) flags |= GnssLocation::HAS_HORIZONTAL_ACCURACY;
    out.gnssLocationFlags = flags;
    out.latitudeDegrees = loc->latitude;
    out.longitudeDegrees = loc->longitude;
    out.altitudeMeters = loc->altitude;
    out.speedMetersPerSec = loc->speed;
    out.bearingDegrees = loc->bearing;
    out.horizontalAccuracyMeters = loc->accuracy;
    out.timestampMillis = loc->timestamp;

    auto st = cb->gnssLocationCb(out);
    if (!st.isOk()) ALOGW("gnssLocationCb failed: %s", st.getDescription().c_str());
}

void statusCb(GpsStatus* status) {
    auto cb = getCb();
    if (!cb || !status) return;
    auto val = static_cast<IGnssCallback::GnssStatusValue>(status->status);
    auto st = cb->gnssStatusCb(val);
    if (!st.isOk()) ALOGW("gnssStatusCb failed");
}

void gpsSvStatusCb(GpsSvStatus* svStatus) {
    auto cb = getCb();
    if (!cb || !svStatus || !gSvEnabled) return;

    std::vector<IGnssCallback::GnssSvInfo> list;
    int n = svStatus->num_svs;
    if (n < 0) n = 0;
    if (n > GPS_MAX_SVS) n = GPS_MAX_SVS;
    list.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto& s = svStatus->sv_list[i];
        IGnssCallback::GnssSvInfo info{};
        info.svid = s.prn;
        info.constellation = GnssConstellationType::GPS;
        info.cN0Dbhz = s.snr;
        info.elevationDegrees = s.elevation;
        info.azimuthDegrees = s.azimuth;
        int32_t fl = 0;
        if (s.prn >= 1 && s.prn <= 32) {
            uint32_t bit = 1u << (s.prn - 1);
            if (svStatus->ephemeris_mask & bit) fl |= 1;
            if (svStatus->almanac_mask & bit) fl |= 2;
            if (svStatus->used_in_fix_mask & bit) fl |= 4;
        }
        info.svFlag = fl;
        list.push_back(info);
    }
    cb->gnssSvStatusCb(list);
}

void gnssSvStatusCb(GnssSvStatus* svStatus) {
    auto cb = getCb();
    if (!cb || !svStatus || !gSvEnabled) return;

    std::vector<IGnssCallback::GnssSvInfo> list;
    int n = svStatus->num_svs;
    if (n < 0) n = 0;
    if (n > GNSS_MAX_SVS) n = GNSS_MAX_SVS;
    list.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto& s = svStatus->gnss_sv_list[i];
        IGnssCallback::GnssSvInfo info{};
        info.svid = s.svid;
        info.constellation = static_cast<GnssConstellationType>(s.constellation);
        info.cN0Dbhz = s.c_n0_dbhz;
        info.elevationDegrees = s.elevation;
        info.azimuthDegrees = s.azimuth;
        info.svFlag = static_cast<int32_t>(s.flags & 0x07);
        list.push_back(info);
    }
    cb->gnssSvStatusCb(list);
}

void nmeaCb(GpsUtcTime timestamp, const char* nmea, int length) {
    auto cb = getCb();
    if (!cb || !nmea || length <= 0 || !gNmeaEnabled) return;
    cb->gnssNmeaCb(timestamp, std::string(nmea, static_cast<size_t>(length)));
}

void setCapabilitiesCb(uint32_t caps) {
    auto cb = getCb();
    if (!cb) return;
    int32_t out = 0;
    if (caps & GPS_CAPABILITY_SCHEDULING) out |= IGnssCallback::CAPABILITY_SCHEDULING;
    if (caps & GPS_CAPABILITY_MSB) out |= IGnssCallback::CAPABILITY_MSB;
    if (caps & GPS_CAPABILITY_MSA) out |= IGnssCallback::CAPABILITY_MSA;
    if (caps & GPS_CAPABILITY_SINGLE_SHOT) out |= IGnssCallback::CAPABILITY_SINGLE_SHOT;
    if (caps & GPS_CAPABILITY_ON_DEMAND_TIME) out |= IGnssCallback::CAPABILITY_ON_DEMAND_TIME;
    ALOGI("GNSS capabilities=0x%x -> aidl=0x%x", caps, out);
    cb->gnssSetCapabilitiesCb(out);
}

void acquireWakelockCb() {
    auto cb = getCb();
    if (cb) cb->gnssAcquireWakelockCb();
}

void releaseWakelockCb() {
    auto cb = getCb();
    if (cb) cb->gnssReleaseWakelockCb();
}

void requestUtcTimeCb() {
    auto cb = getCb();
    if (cb) cb->gnssRequestTimeCb();
}

void setSystemInfoCb(const GnssSystemInfo* info) {
    auto cb = getCb();
    if (!cb || !info) return;
    IGnssCallback::GnssSystemInfo out{};
    out.yearOfHw = info->year_of_hw;
    out.name = "BCM4773";
    cb->gnssSetSystemInfoCb(out);
}

void agpsStatusCb(AGpsStatus* /*status*/) {}
void gpsNiNotifyCb(GpsNiNotification* /*notification*/) {}
void gpsXtraDownloadReqCb() {}
void agpsRilReqSetIdCb(uint32_t /*flags*/) {}
void agpsRilReqRefLocCb(uint32_t /*flags*/) {}
void gfTransitionCb(int32_t, GpsLocation*, int32_t, GpsUtcTime) {}
void gfStatusCb(int32_t, GpsLocation*) {}
void gfAddCb(int32_t, int32_t) {}
void gfRemoveCb(int32_t, int32_t) {}
void gfPauseCb(int32_t, int32_t) {}
void gfResumeCb(int32_t, int32_t) {}

AGpsCallbacks gAgpsCbs = {
    .status_cb = agpsStatusCb,
    .create_thread_cb = createThreadCb,
};

GpsNiCallbacks gNiCbs = {
    .notify_cb = gpsNiNotifyCb,
    .create_thread_cb = createThreadCb,
};

GpsXtraCallbacks gXtraCbs = {
    .download_request_cb = gpsXtraDownloadReqCb,
    .create_thread_cb = createThreadCb,
};

AGpsRilCallbacks gAgpsRilCbs = {
    .request_setid = agpsRilReqSetIdCb,
    .request_refloc = agpsRilReqRefLocCb,
    .create_thread_cb = createThreadCb,
};

GpsGeofenceCallbacks gGeofenceCbs = {
    .geofence_transition_callback = gfTransitionCb,
    .geofence_status_callback = gfStatusCb,
    .geofence_add_callback = gfAddCb,
    .geofence_remove_callback = gfRemoveCb,
    .geofence_pause_callback = gfPauseCb,
    .geofence_resume_callback = gfResumeCb,
    .create_thread_cb = createThreadCb,
};

GpsCallbacks gLegacyCbs = {
    .size = sizeof(GpsCallbacks),
    .location_cb = locationCb,
    .status_cb = statusCb,
    .sv_status_cb = gpsSvStatusCb,
    .nmea_cb = nmeaCb,
    .set_capabilities_cb = setCapabilitiesCb,
    .acquire_wakelock_cb = acquireWakelockCb,
    .release_wakelock_cb = releaseWakelockCb,
    .create_thread_cb = createThreadCb,
    .request_utc_time_cb = requestUtcTimeCb,
    .set_system_info_cb = setSystemInfoCb,
    .gnss_sv_status_cb = gnssSvStatusCb,
};

}  // namespace

class ExynosGnss : public BnGnss {
public:
    explicit ExynosGnss(const GpsInterface* iface) : mIface(iface) {}

    ScopedAStatus setCallback(const std::shared_ptr<IGnssCallback>& callback) override {
        if (!mIface || !callback) {
            return ScopedAStatus::fromServiceSpecificError(ERROR_INVALID_ARGUMENT);
        }
        {
            std::lock_guard<std::mutex> lk(gCbMutex);
            gCallback = callback;
        }
        int ret = mIface->init(&gLegacyCbs);
        ALOGI("mIface->init() -> %d", ret);
        if (ret != 0) {
            return ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
        }
        if (mIface->get_extension) {
            if (auto* ext = static_cast<const AGpsRilInterface*>(mIface->get_extension(AGPS_RIL_INTERFACE))) {
                if (ext->init) ext->init(&gAgpsRilCbs);
            }
            if (auto* ext = static_cast<const AGpsInterface*>(mIface->get_extension(AGPS_INTERFACE))) {
                if (ext->init) ext->init(&gAgpsCbs);
            }
            if (auto* ext = static_cast<const GpsNiInterface*>(mIface->get_extension(GPS_NI_INTERFACE))) {
                if (ext->init) ext->init(&gNiCbs);
            }
            if (auto* ext = static_cast<const GpsXtraInterface*>(mIface->get_extension(GPS_XTRA_INTERFACE))) {
                if (ext->init) ext->init(&gXtraCbs);
            }
            if (auto* ext = static_cast<const GpsGeofencingInterface*>(mIface->get_extension(GPS_GEOFENCING_INTERFACE))) {
                if (ext->init) ext->init(&gGeofenceCbs);
            }
        }
        IGnssCallback::GnssSystemInfo sys{};
        sys.yearOfHw = 2015;
        sys.name = "BCM4773";
        callback->gnssSetSystemInfoCb(sys);
        return ScopedAStatus::ok();
    }

    ScopedAStatus close() override {
        if (mIface) mIface->cleanup();
        std::lock_guard<std::mutex> lk(gCbMutex);
        gCallback.reset();
        return ScopedAStatus::ok();
    }

    ScopedAStatus start() override {
        if (!mIface) return ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
        int ret = mIface->start();
        ALOGI("mIface->start() -> %d", ret);
        return ret == 0 ? ScopedAStatus::ok()
                        : ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
    }

    ScopedAStatus stop() override {
        if (!mIface) return ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
        int ret = mIface->stop();
        ALOGI("mIface->stop() -> %d", ret);
        return ret == 0 ? ScopedAStatus::ok()
                        : ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
    }

    ScopedAStatus injectTime(int64_t timeMs, int64_t timeReferenceMs, int32_t uncertaintyMs) override {
        if (mIface && mIface->inject_time) {
            mIface->inject_time(timeMs, timeReferenceMs, uncertaintyMs);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus injectLocation(const GnssLocation& loc) override {
        if (mIface && mIface->inject_location) {
            mIface->inject_location(loc.latitudeDegrees, loc.longitudeDegrees,
                                    loc.horizontalAccuracyMeters);
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus injectBestLocation(const GnssLocation& loc) override {
        return injectLocation(loc);
    }

    ScopedAStatus deleteAidingData(IGnss::GnssAidingData flags) override {
        if (mIface && mIface->delete_aiding_data) {
            mIface->delete_aiding_data(static_cast<GpsAidingData>(flags));
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus setPositionMode(const IGnss::PositionModeOptions& options) override {
        if (!mIface || !mIface->set_position_mode) {
            return ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
        }
        int ret = mIface->set_position_mode(
            static_cast<GpsPositionMode>(options.mode),
            static_cast<GpsPositionRecurrence>(options.recurrence),
            static_cast<uint32_t>(options.minIntervalMs),
            static_cast<uint32_t>(options.preferredAccuracyMeters),
            static_cast<uint32_t>(options.preferredTimeMs));
        return ret == 0 ? ScopedAStatus::ok()
                        : ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
    }

    ScopedAStatus startSvStatus() override { gSvEnabled = true; return ScopedAStatus::ok(); }
    ScopedAStatus stopSvStatus() override { gSvEnabled = false; return ScopedAStatus::ok(); }
    ScopedAStatus startNmea() override { gNmeaEnabled = true; return ScopedAStatus::ok(); }
    ScopedAStatus stopNmea() override { gNmeaEnabled = false; return ScopedAStatus::ok(); }

    ScopedAStatus getExtensionPsds(std::shared_ptr<IGnssPsds>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionGnssConfiguration(std::shared_ptr<IGnssConfiguration>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionGnssMeasurement(std::shared_ptr<IGnssMeasurementInterface>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionGnssPowerIndication(std::shared_ptr<IGnssPowerIndication>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionGnssBatching(std::shared_ptr<IGnssBatching>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionGnssGeofence(std::shared_ptr<IGnssGeofence>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionGnssNavigationMessage(std::shared_ptr<IGnssNavigationMessageInterface>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionAGnss(std::shared_ptr<IAGnss>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionAGnssRil(std::shared_ptr<IAGnssRil>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionGnssDebug(std::shared_ptr<IGnssDebug>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionGnssVisibilityControl(std::shared_ptr<visibility_control::IGnssVisibilityControl>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionGnssAntennaInfo(std::shared_ptr<IGnssAntennaInfo>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }
    ScopedAStatus getExtensionMeasurementCorrections(std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface>* out) override { *out = nullptr; return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }

private:
    const GpsInterface* mIface;
};

}  // namespace aidl::android::hardware::gnss

int main() {
    ALOGI("Starting Exynos7420 AIDL GNSS HAL service (BCM4773)");

    void* handle = dlopen("/vendor/lib64/hw/gps.universal7420.so", RTLD_NOW);
    if (!handle) {
        ALOGE("dlopen(gps.universal7420.so) failed: %s", dlerror());
        return 1;
    }

    auto* hmi = static_cast<hw_module_t*>(dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR));
    if (!hmi || !hmi->methods || !hmi->methods->open) {
        ALOGE("HMI symbol missing in gps.universal7420.so");
        return 1;
    }

    hw_device_t* dev = nullptr;
    int rc = hmi->methods->open(hmi, GPS_HARDWARE_MODULE_ID, &dev);
    if (rc != 0 || !dev) {
        ALOGE("gps module open failed: %d", rc);
        return 1;
    }

    auto* gpsDev = reinterpret_cast<gps_device_t*>(dev);
    const GpsInterface* iface = gpsDev->get_gps_interface(gpsDev);
    if (!iface) {
        ALOGE("get_gps_interface returned nullptr");
        return 1;
    }

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    auto service = ndk::SharedRefBase::make<aidl::android::hardware::gnss::ExynosGnss>(iface);
    const std::string instance = std::string(aidl::android::hardware::gnss::IGnss::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(service->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        ALOGE("AServiceManager_addService(%s) failed: %d", instance.c_str(), status);
        return 1;
    }

    ALOGI("Registered %s successfully", instance.c_str());
    ABinderProcess_joinThreadPool();
    return 0;
}
