#include <cstddef>
#include <thread>
#include <chrono>
#include <iostream>

#include "TestJNI.h"

// create a instance, and manipulate the member variable value
JNIEXPORT jobject JNICALL nativeCreateInnerClassInstance
  (JNIEnv *env, jclass cls, jstring msg, jint count) {
  jclass inner_class = env->FindClass("TestJNI$InnerClass");
  jmethodID constructor = env->GetMethodID(inner_class, "<init>", "()V");

  // create a new instance
  jobject obj = env->NewObject(inner_class, constructor);

  // manipulate the inner class member variable.
  jfieldID count_fid = env->GetFieldID(inner_class, "count", "I");
  env->SetIntField(obj, count_fid, count);

  jfieldID message_fid = env->GetFieldID(inner_class, "message", "Ljava/lang/String;");
  env->SetObjectField(obj, message_fid, msg);

  return obj;
}

// get the member variable from JNI
JNIEXPORT void JNICALL nativePrintInnerClassInstance
  (JNIEnv *env, jclass cls, jobject obj) {
  jclass inner_class = env->FindClass("TestJNI$InnerClass");

  jfieldID count_fid = env->GetFieldID(inner_class, "count", "I");
  jint count = env->GetIntField(obj, count_fid);

  jfieldID msg_fid = env->GetFieldID(inner_class, "message", "Ljava/lang/String;");
  jstring message = (jstring) env->GetObjectField(obj, msg_fid);

  for (int i = 0; i < count; ++i) {
    std::cout << env->GetStringUTFChars(message, 0) << '\n';
  }
}

JavaVM *jvm;
jobject g_obj; // you need create a global variable when passing a jobject argument

// one process only have one jvm.
// Q: Why I can not pass the reference ?
// A: seems like JNI provides wrapper clesses. more detail see NewGlobalRef JNI.
void attach() {
  using namespace std::literals::chrono_literals; // c++14, for `1s` syntax.
  JNIEnv* env;

  jvm->AttachCurrentThread((void **)&env, NULL);
  // jobject global_obj = env->NewGlobalRef(obj);
  jclass inner_class = env->FindClass("TestJNI$InnerClass");
  jmethodID print_mid = env->GetMethodID(inner_class, "print", "()V");

  for (int i = 0; i < 1; ++i) {
    std::cout << "\ncall the print() method" << '\n';
    // env->CallVoidMethod(global_obj, print_mid);
    if (g_obj != nullptr)
      env->CallVoidMethod(g_obj, print_mid);

    std::this_thread::sleep_for(1s);
  }
  std::cout << "finished...." << '\n';

  env->DeleteGlobalRef(g_obj);
  jvm->DetachCurrentThread();
}

// call java method from c++ code
JNIEXPORT void JNICALL nativePrintInnerClassInstanceByCallingJavaMethod
  (JNIEnv *env, jclass cls, jobject obj) {
  env->GetJavaVM(&jvm);
  std::cout << "create a new thread, and pass the arugment: " << obj << '\n';

  // jclass inner_class = env->FindClass("TestJNI$InnerClass");
  // jmethodID print_mid = env->GetMethodID(inner_class, "print", "()V");
  // env->CallVoidMethod(obj, print_mid);

  g_obj = env->NewGlobalRef(obj);
  std::thread t(&attach);

  // sleep for 1 seconds to wait for java code to start. Any idea to slove it ?
  // create global references before creating the thread.
  //
  // using namespace std::literals::chrono_literals;
  // std::this_thread::sleep_for(1s);

  t.detach();
}

jclass g_cls; // load it when JNI_OnLoad method called, reuse it later

void staticPrint() {
  using namespace std::literals::chrono_literals;
  JNIEnv *env;

  jvm->AttachCurrentThread((void **)&env, NULL);

  // NOTE: the following code may not work in android device, and get the error,
  // TestJNI$InnerClass couldn't be found. so we must find the class in JNI part.
  // jclass inner_class = env->FindClass("TestJNI$InnerClass");
  // jmethodID static_mid = env->GetStaticMethodID(inner_class, "staticPrint", "()V");

  // Use the global class, we set it when loading.
  jmethodID static_mid = env->GetStaticMethodID(g_cls, "staticPrint", "()V");

  while (true) {
    env->CallStaticVoidMethod(g_cls, static_mid/*, no arguments*/);
    std::this_thread::sleep_for(1s);
  }

  jvm->DetachCurrentThread();
}

JNIEXPORT void JNICALL nativeCallInnerClassStaticPrintMethodNativeThread
  (JNIEnv *env, jclass cls) {
  env->GetJavaVM(&jvm);
  std::thread t(&staticPrint);
  t.join();
}

void registerNative(JNIEnv *env) {
  const char* TestJNIClassPathName = "TestJNI";

  JNINativeMethod TestJNINativeMethods[] = {
    {
      (char*)"nativeCreateInnerClassInstance",
      (char*)"(Ljava/lang/String;I)LTestJNI$InnerClass;",
      reinterpret_cast<void*>(
          static_cast<jobject (*)(JNIEnv*, jclass, jstring, jint)>
          (&nativeCreateInnerClassInstance))
    },
    {
      (char*)"nativePrintInnerClassInstance",
      (char*)"(LTestJNI$InnerClass;)V",
      reinterpret_cast<void*>(
          static_cast<void (*)(JNIEnv*, jclass, jobject)>
          (&nativePrintInnerClassInstance))
    },
    {
      (char*)"nativePrintInnerClassInstanceByCallingJavaMethod",
      (char*)"(LTestJNI$InnerClass;)V",
      reinterpret_cast<void*>(
          static_cast<void (*)(JNIEnv*, jclass, jobject)>
          (&nativePrintInnerClassInstanceByCallingJavaMethod))
    },
    {
      (char *)"nativeCallInnerClassStaticPrintMethodNativeThread",
      (char *)"()V",
      reinterpret_cast<void*>(
          static_cast<void (*)(JNIEnv*, jclass)>
          (&nativeCallInnerClassStaticPrintMethodNativeThread))
    },
  };

  jclass cls = env->FindClass(TestJNIClassPathName);
  env->RegisterNatives(cls, TestJNINativeMethods,
      sizeof(TestJNINativeMethods) / sizeof(TestJNINativeMethods[0]));

  // Find the class when loading, and reuse it later.
  g_cls = env->FindClass("TestJNI$InnerClass");
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved) {
  JNIEnv *env;
  if (jvm->GetEnv((void**) &env, JNI_VERSION_1_4) != JNI_OK) {
    return -1;
  }

  // register all native methods
  registerNative(env);

  return JNI_VERSION_1_4;
}
