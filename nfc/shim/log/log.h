#pragma once
#include <android/log.h>
#ifndef LOG_TAG
#define LOG_TAG "SecHAL"
#endif
#define ALOG(prio, tag, ...) __android_log_print(ANDROID_##prio, tag, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define ALOGD_IF(cond, ...) do { if (cond) ALOGD(__VA_ARGS__); } while (0)
#define ALOGE_IF(cond, ...) do { if (cond) ALOGE(__VA_ARGS__); } while (0)
