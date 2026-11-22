#define LOG_TAG "sensors.max86900"

#include <V2_1/SubHal.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace android::hardware::sensors::V2_1::implementation::zeroflte {

using ::android::sp;
using ::android::base::ReadFileToString;
using ::android::base::StringPrintf;
using ::android::base::Trim;
using ::android::base::WriteStringToFd;
using ::android::base::WriteStringToFile;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::sensors::V1_0::MetaDataEventType;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::RateLevel;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorFlagBits;
using ::android::hardware::sensors::V1_0::SensorStatus;
using ::android::hardware::sensors::V1_0::SharedMemInfo;

namespace {

constexpr int32_t kHeartRateHandle = 1;
constexpr uint16_t kFingerIrThreshold = 6000;
constexpr char kIioDevicesDir[] = "/sys/bus/iio/devices";
constexpr char kTargetIioName[] = "max86900-hrm";

struct Max86900ScanRecord {
    int32_t bpm;
    uint16_t ir;
    uint16_t red;
    int64_t timestampNs;
};
static_assert(sizeof(Max86900ScanRecord) == 16, "max86900 IIO scan size mismatch");
static_assert(sizeof(Event) == 80, "HIDL V2_1::Event size mismatch");
static_assert(sizeof(SensorInfo) == 112, "HIDL V2_1::SensorInfo size mismatch");

int64_t getElapsedRealtimeNs() {
    struct timespec ts {};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

bool findMax86900Device(std::string* outSysfsDir, std::string* outDevNode) {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kIioDevicesDir), closedir);
    if (!dir) {
        PLOG(ERROR) << "Cannot open " << kIioDevicesDir;
        return false;
    }

    while (struct dirent* ent = readdir(dir.get())) {
        if (std::strncmp(ent->d_name, "iio:device", 10) != 0) continue;
        std::string sysDir = std::string(kIioDevicesDir) + "/" + ent->d_name;
        std::string name;
        if (!ReadFileToString(sysDir + "/name", &name)) continue;
        if (Trim(name) == kTargetIioName) {
            *outSysfsDir = sysDir;
            *outDevNode = std::string("/dev/") + ent->d_name;
            return true;
        }
    }
    return false;
}

}  // namespace

class Max86900SubHal : public ISensorsSubHal {
  public:
    Max86900SubHal() {
        mInfo.sensorHandle = kHeartRateHandle;
        mInfo.name = "MAX86900 Heart Rate";
        mInfo.vendor = "Maxim Integrated";
        mInfo.version = 1;
        mInfo.type = SensorType::HEART_RATE;
        mInfo.typeAsString = "";
        mInfo.maxRange = 250.0f;
        mInfo.resolution = 1.0f;
        mInfo.power = 5.0f;
        mInfo.minDelay = 0;
        mInfo.fifoReservedEventCount = 0;
        mInfo.fifoMaxEventCount = 0;
        mInfo.requiredPermission = "android.permission.BODY_SENSORS";
        mInfo.maxDelay = 0;
        mInfo.flags = static_cast<uint32_t>(SensorFlagBits::ON_CHANGE_MODE);

        if (findMax86900Device(&mSysfsDir, &mDevNode)) {
            mHasSensor = validateScanElements();
        }
        if (!mHasSensor) {
            LOG(WARNING) << "max86900-hrm IIO device not found; reporting empty sensor list";
            return;
        }

        mWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (mWakeFd < 0) {
            PLOG(ERROR) << "eventfd failed";
            mHasSensor = false;
            return;
        }
        mThread = std::thread(&Max86900SubHal::run, this);
        LOG(INFO) << "Initialized max86900 sub-HAL at " << mSysfsDir << " (" << mDevNode << ")";
    }

    ~Max86900SubHal() override {
        mStop = true;
        wake();
        if (mThread.joinable()) mThread.join();
        int fd = mFd.exchange(-1);
        if (fd >= 0) close(fd);
        if (mWakeFd >= 0) {
            close(mWakeFd);
            mWakeFd = -1;
        }
    }

    Return<void> getSensorsList_2_1(getSensorsList_2_1_cb callback) override {
        std::vector<SensorInfo> list;
        if (mHasSensor) {
            list.push_back(mInfo);
        }
        callback(list);
        return Void();
    }

    Return<Result> setOperationMode(OperationMode mode) override {
        return mode == OperationMode::NORMAL ? Result::OK : Result::BAD_VALUE;
    }

    Return<Result> activate(int32_t sensorHandle, bool enabled) override {
        if (!mHasSensor || sensorHandle != kHeartRateHandle) {
            return Result::BAD_VALUE;
        }

        std::lock_guard<std::mutex> lock(mLock);
        if (enabled == mEnabled) {
            return Result::OK;
        }

        if (enabled) {
            if (!enableScanAndBuffer()) {
                return Result::BAD_VALUE;
            }
            mEnabled = true;
            mClosePending = false;
            mFirstEvent = true;
            mLastBpm = -1.0f;
            mLastStatus = SensorStatus::UNRELIABLE;
            mLastEventNs = 0;
            wake();
        } else {
            WriteStringToFile("0", mSysfsDir + "/buffer/enable");
            mEnabled = false;
            mFlushPending = 0;
            mClosePending = true;
            wake();
        }
        return Result::OK;
    }

    Return<Result> batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                         int64_t /* maxReportLatencyNs */) override {
        if (!mHasSensor || sensorHandle != kHeartRateHandle || samplingPeriodNs < 0) {
            return Result::BAD_VALUE;
        }
        std::lock_guard<std::mutex> lock(mLock);
        mPeriodNs = samplingPeriodNs;
        return Result::OK;
    }

    Return<Result> flush(int32_t sensorHandle) override {
        if (!mHasSensor || sensorHandle != kHeartRateHandle) {
            return Result::BAD_VALUE;
        }
        std::lock_guard<std::mutex> lock(mLock);
        if (!mEnabled) {
            return Result::BAD_VALUE;
        }
        mFlushPending++;
        wake();
        return Result::OK;
    }

    Return<Result> injectSensorData_2_1(const Event& /* event */) override {
        return Result::INVALID_OPERATION;
    }

    Return<void> registerDirectChannel(const SharedMemInfo& /* mem */,
                                       registerDirectChannel_cb callback) override {
        callback(Result::INVALID_OPERATION, -1);
        return Void();
    }

    Return<Result> unregisterDirectChannel(int32_t /* channelHandle */) override {
        return Result::INVALID_OPERATION;
    }

    Return<void> configDirectReport(int32_t /* sensorHandle */, int32_t /* channelHandle */,
                                    RateLevel /* rate */, configDirectReport_cb callback) override {
        callback(Result::INVALID_OPERATION, 0);
        return Void();
    }

    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& /* args */) override {
        if (fd.getNativeHandle() == nullptr || fd->numFds < 1) return Void();
        std::string out;
        {
            std::lock_guard<std::mutex> lock(mLock);
            out = StringPrintf("MAX86900SubHal: present=%d enabled=%d sysfs=%s dev=%s "
                               "records=%llu periodNs=%lld lastBpm=%.1f lastStatus=%d\n",
                               mHasSensor ? 1 : 0, mEnabled ? 1 : 0, mSysfsDir.c_str(),
                               mDevNode.c_str(), static_cast<unsigned long long>(mRecords),
                               static_cast<long long>(mPeriodNs), mLastBpm,
                               static_cast<int>(mLastStatus));
        }
        WriteStringToFd(out, fd->data[0]);
        return Void();
    }

    const std::string getName() override { return "max86900-hrm"; }

    Return<Result> initialize(const sp<IHalProxyCallback>& halProxyCallback) override {
        if (mHasSensor) {
            activate(kHeartRateHandle, false);
        }
        std::lock_guard<std::mutex> lock(mCallbackLock);
        mCallback = halProxyCallback;
        return Result::OK;
    }

  private:
    bool validateScanElements() {
        static const char* const kRequired[] = {"in_count", "in_intensity_ir", "in_intensity_red",
                                                "in_timestamp"};
        for (const char* name : kRequired) {
            std::string idxPath = mSysfsDir + "/scan_elements/" + name + "_index";
            if (access(idxPath.c_str(), R_OK) != 0) {
                LOG(WARNING) << "Missing scan element: " << idxPath;
                return false;
            }
        }
        return true;
    }

    bool enableScanAndBuffer() {
        static const char* const kEnables[] = {
                "scan_elements/in_count_en",
                "scan_elements/in_intensity_ir_en",
                "scan_elements/in_intensity_red_en",
                "scan_elements/in_timestamp_en",
        };
        for (const char* attr : kEnables) {
            if (!WriteStringToFile("1", mSysfsDir + "/" + attr)) {
                PLOG(ERROR) << "Failed enabling " << mSysfsDir << "/" << attr;
                return false;
            }
        }
        if (!WriteStringToFile("64", mSysfsDir + "/buffer/length")) {
            PLOG(ERROR) << "Failed setting buffer/length at " << mSysfsDir;
            return false;
        }
        int oldFd = mFd.load();
        if (oldFd >= 0) {
            close(oldFd);
            mFd = -1;
        }
        int fd = open(mDevNode.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            PLOG(ERROR) << "Failed opening " << mDevNode;
            return false;
        }
        if (!WriteStringToFile("1", mSysfsDir + "/buffer/enable")) {
            PLOG(ERROR) << "Failed enabling buffer at " << mSysfsDir;
            close(fd);
            return false;
        }
        mFd = fd;
        return true;
    }

    void wake() {
        const uint64_t one = 1;
        if (mWakeFd >= 0) (void)!write(mWakeFd, &one, sizeof(one));
    }

    void postEvents(const std::vector<Event>& events) {
        if (events.empty()) return;
        sp<IHalProxyCallback> cb;
        {
            std::lock_guard<std::mutex> lock(mCallbackLock);
            cb = mCallback;
        }
        if (cb != nullptr) {
            cb->postEvents(events, cb->createScopedWakelock(false));
        }
    }

    void readRecords(int fd) {
        Max86900ScanRecord recs[16];
        std::vector<Event> events;

        for (;;) {
            ssize_t n = read(fd, recs, sizeof(recs));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) {
                if (errno != EAGAIN) PLOG(ERROR) << "read(" << mDevNode << ") failed";
                break;
            }
            if (n == 0) break;

            size_t count = static_cast<size_t>(n) / sizeof(Max86900ScanRecord);
            std::lock_guard<std::mutex> lock(mLock);
            if (!mEnabled) break;

            for (size_t i = 0; i < count; ++i) {
                const Max86900ScanRecord& r = recs[i];
                mRecords++;

                float bpm = 0.0f;
                SensorStatus status = SensorStatus::NO_CONTACT;
                if (r.ir >= kFingerIrThreshold) {
                    if (r.bpm > 0) {
                        bpm = std::min(static_cast<float>(r.bpm), 250.0f);
                        status = SensorStatus::ACCURACY_MEDIUM;
                    } else {
                        bpm = 0.0f;
                        status = SensorStatus::UNRELIABLE;
                    }
                }

                int64_t tsNs = r.timestampNs > 0 ? r.timestampNs : getElapsedRealtimeNs();
                bool changed = mFirstEvent || (status != mLastStatus) || (bpm != mLastBpm);
                bool rateOk = mFirstEvent || (status != mLastStatus) || (mPeriodNs <= 0) ||
                              (tsNs - mLastEventNs >= mPeriodNs);

                if (changed && rateOk) {
                    Event e{};
                    e.timestamp = tsNs;
                    e.sensorHandle = kHeartRateHandle;
                    e.sensorType = SensorType::HEART_RATE;
                    e.u.heartRate.bpm = bpm;
                    e.u.heartRate.status = status;
                    events.push_back(e);

                    mFirstEvent = false;
                    mLastBpm = bpm;
                    mLastStatus = status;
                    mLastEventNs = tsNs;
                }
            }
        }
        postEvents(events);
    }

    void run() {
        while (!mStop) {
            bool doClose = false;
            {
                std::lock_guard<std::mutex> lock(mLock);
                if (mClosePending && !mEnabled) {
                    doClose = true;
                    mClosePending = false;
                }
            }
            if (doClose) {
                int oldFd = mFd.exchange(-1);
                if (oldFd >= 0) close(oldFd);
            }

            int fd = mFd.load();
            struct pollfd fds[2] = {{mWakeFd, POLLIN, 0}, {fd, POLLIN, 0}};
            if (poll(fds, fd >= 0 ? 2 : 1, -1) < 0) {
                if (errno != EINTR) {
                    PLOG(ERROR) << "poll failed";
                    usleep(100000);
                }
                continue;
            }

            if (fds[0].revents & POLLIN) {
                uint64_t v;
                (void)!read(mWakeFd, &v, sizeof(v));
            }

            if (fd >= 0) {
                if (fds[1].revents & POLLIN) {
                    readRecords(fd);
                }
                if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    LOG(ERROR) << mDevNode << " poll error";
                    usleep(200000);
                }
            }

            unsigned flushes = 0;
            {
                std::lock_guard<std::mutex> lock(mLock);
                flushes = mFlushPending;
                mFlushPending = 0;
            }
            if (flushes > 0) {
                Event e{};
                e.sensorHandle = kHeartRateHandle;
                e.sensorType = SensorType::META_DATA;
                e.timestamp = getElapsedRealtimeNs();
                e.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;
                postEvents(std::vector<Event>(flushes, e));
            }
        }
    }

    SensorInfo mInfo{};
    std::string mSysfsDir;
    std::string mDevNode;
    bool mHasSensor = false;

    std::mutex mLock;
    bool mEnabled = false;
    bool mClosePending = false;
    bool mFirstEvent = true;
    int64_t mPeriodNs = 0;
    int64_t mLastEventNs = 0;
    float mLastBpm = -1.0f;
    SensorStatus mLastStatus = SensorStatus::UNRELIABLE;
    uint64_t mRecords = 0;
    unsigned mFlushPending = 0;

    std::mutex mCallbackLock;
    sp<IHalProxyCallback> mCallback;

    std::atomic<int> mFd{-1};
    int mWakeFd = -1;
    std::atomic<bool> mStop{false};
    std::thread mThread;
};

}  // namespace android::hardware::sensors::V2_1::implementation::zeroflte

extern "C" __attribute__((visibility("default")))
::android::hardware::sensors::V2_1::implementation::ISensorsSubHal*
sensorsHalGetSubHal_2_1(uint32_t* version) {
    static ::android::hardware::sensors::V2_1::implementation::zeroflte::Max86900SubHal subHal;
    *version = SUB_HAL_2_1_VERSION;
    return &subHal;
}
