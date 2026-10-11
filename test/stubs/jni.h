// Host-build stub for the Android JNI header.
//
// Used only by the `bitematica_android_syntax` check, which compiles the preloader glue
// on a host with stubbed Android/fmt headers so a type error in that file is caught
// without an NDK. Never shipped.
#pragma once
struct JavaVM {};
