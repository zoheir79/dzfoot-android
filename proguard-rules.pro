-keep class com.football.ar.** { *; }
-keep class com.google.ar.core.** { *; }

# Godot Engine
-keep class org.godotengine.godot.** { *; }
-keep class org.godotengine.godot.xr.** { *; }
-keep class org.godotengine.godot.utils.** { *; }
-keep class org.godotengine.godot.io.** { *; }
-dontwarn org.godotengine.godot.**

# GDExtension native library
-keep class dzfoot.** { *; }

-keepattributes Exceptions,InnerClasses,Signature,Deprecated,SourceFile,LineNumberTable,*Annotation*,EnclosingMethod
-keepclassmembers class * {
    @android.webkit.JavascriptInterface <methods>;
}
-keep class com.football.ar.JniBridge {
    public <methods>;
}
