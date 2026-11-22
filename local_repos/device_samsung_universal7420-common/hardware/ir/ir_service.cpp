#define LOG_TAG "ConsumerIrExynos"

#include <aidl/android/hardware/ir/BnConsumerIr.h>
#include <aidl/android/hardware/ir/ConsumerIrFreqRange.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace aidl::android::hardware::ir {

using ::ndk::ScopedAStatus;

namespace {

constexpr const char* kIrSendPath = "/sys/class/sensors/irled_remote/irled_send";
constexpr const char* kIrResultPath = "/sys/class/sensors/irled_remote/irled_send_result";
constexpr int32_t kMinFreqHz = 16000;
constexpr int32_t kMaxFreqHz = 60000;
constexpr size_t kMaxPayloadLen = 1020;

int writeIrPattern(const std::string& payload) {
    int fd = -1;
    for (int attempt = 0; attempt < 10; ++attempt) {
        fd = open(kIrSendPath, O_WRONLY | O_CLOEXEC);
        if (fd >= 0) break;
        if (errno != EINTR && errno != EAGAIN && errno != EACCES) {
            return -errno;
        }
        sched_yield();
    }
    if (fd < 0) return -errno;

    const char* ptr = payload.data();
    size_t left = payload.size();
    int err = 0;
    while (left > 0) {
        ssize_t n = write(fd, ptr, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            err = -errno;
            break;
        }
        ptr += n;
        left -= static_cast<size_t>(n);
    }
    close(fd);
    return err;
}

int readIrResult() {
    int fd = open(kIrResultPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    char buf[16] = {};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    int saved = errno;
    close(fd);
    if (n <= 0) return n < 0 ? -saved : -EIO;
    return atoi(buf);
}

}  // namespace

class ExynosConsumerIr : public BnConsumerIr {
public:
    ScopedAStatus getCarrierFreqs(std::vector<ConsumerIrFreqRange>* _aidl_return) override {
        if (!_aidl_return) {
            return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        if (access(kIrSendPath, F_OK) != 0) {
            *_aidl_return = {};
            return ScopedAStatus::ok();
        }
        ConsumerIrFreqRange range{};
        range.minHz = kMinFreqHz;
        range.maxHz = kMaxFreqHz;
        *_aidl_return = {range};
        return ScopedAStatus::ok();
    }

    ScopedAStatus transmit(int32_t in_carrierFreqHz,
                           const std::vector<int32_t>& in_pattern) override {
        if (in_carrierFreqHz < kMinFreqHz || in_carrierFreqHz > kMaxFreqHz) {
            return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
        if (in_pattern.empty()) {
            return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }

        std::string payload = std::to_string(in_carrierFreqHz);
        uint64_t totalUs = 0;
        for (int32_t us : in_pattern) {
            if (us <= 0) {
                return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
            totalUs += static_cast<uint64_t>(us);
            payload.push_back(',');
            payload.append(std::to_string(us));
            if (payload.size() >= kMaxPayloadLen) {
                ALOGE("IR pattern exceeds max payload length (%zu >= %zu)",
                      payload.size(), kMaxPayloadLen);
                return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
        }

        std::lock_guard<std::mutex> lk(mMutex);
        int rc = writeIrPattern(payload);
        if (rc < 0) {
            ALOGE("writeIrPattern failed: %d (%s)", rc, strerror(-rc));
            return ScopedAStatus::fromServiceSpecificError(-rc);
        }

        /* Match Samsung HAL timing contract: block for transmit duration */
        if (totalUs > 0 && totalUs < 10000000ULL) {
            usleep(static_cast<useconds_t>(totalUs));
        }
        int res = readIrResult();
        if (res < 0) {
            ALOGW("irled_send_result read returned %d", res);
        } else {
            ALOGI("transmit freq=%d entries=%zu us=%llu result=%d",
                  in_carrierFreqHz, in_pattern.size(),
                  static_cast<unsigned long long>(totalUs), res);
        }
        return ScopedAStatus::ok();
    }

private:
    std::mutex mMutex;
};

}  // namespace aidl::android::hardware::ir

int main() {
    ALOGI("Starting Exynos7420 AIDL ConsumerIr HAL service");
    ABinderProcess_setThreadPoolMaxThreadCount(2);
    auto service = ndk::SharedRefBase::make<aidl::android::hardware::ir::ExynosConsumerIr>();
    const std::string instance =
        std::string(aidl::android::hardware::ir::IConsumerIr::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(service->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        ALOGE("AServiceManager_addService(%s) failed: %d", instance.c_str(), status);
        return 1;
    }
    ALOGI("Registered %s successfully", instance.c_str());
    ABinderProcess_joinThreadPool();
    return 0;
}
