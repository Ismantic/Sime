# Keep all native method declarations — names must match the JNI symbols
# in libsime_jni.so. The C++ side never calls back into Java, so no other
# class members need to be kept on its behalf.
-keepclasseswithmembers class * {
    native <methods>;
}

# Honor explicit @Keep annotations if any are added later.
-keep @androidx.annotation.Keep class * { *; }
-keepclassmembers class * {
    @androidx.annotation.Keep *;
}

# Manifest-declared components (Service / Activity) are auto-kept by AGP,
# no manual rules needed.
