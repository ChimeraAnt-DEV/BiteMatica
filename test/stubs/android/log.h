// Host-build stub for Android's log.h. See test/stubs/jni.h.
#pragma once
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
#ifdef __cplusplus
extern "C" {
#endif
int __android_log_print(int level, const char *tag, const char *format, ...);
#ifdef __cplusplus
}
#endif
