# ProGuard rules for DZFoot
# Keep native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep JNI bridge
-keep class com.football.ar.JniBridge { *; }

# Keep ARCore classes
-keep class com.google.ar.** { *; }

# LiveKit
-keep class io.livekit.android.** { *; }
-keep class org.webrtc.** { *; }
