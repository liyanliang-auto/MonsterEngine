// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Core/Platform.h"
#if PLATFORM_ANDROID
#include <jni.h>
/** Registers the frozen NativeBridge methods. No filesystem/GPU/runtime work. */
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*);
/** Retires registered owners outside registry locks when the VM unloads us. */
extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void*);
#endif
