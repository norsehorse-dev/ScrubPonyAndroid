# Project-specific R8 rules. R8 (minify + resource shrinking) is on for the
# release build type in app/build.gradle.kts.
#
# The one hard requirement is the JNI boundary. libscrubpony_jni.so exports
# statically named symbols (Java_com_norsehorse_scrubpony_NativeScrubber_*),
# so the class and its native method names must survive obfuscation exactly.
# The C side never calls back into Kotlin (no FindClass/GetMethodID), so
# nothing else needs to be kept by name for the native code's sake.
-keepclasseswithmembernames,includedescriptorclasses class com.norsehorse.scrubpony.NativeScrubber {
    native <methods>;
}
