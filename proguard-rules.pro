-keep class com.football.ar.** { *; }
-keep class com.google.ar.core.** { *; }
-keepattributes Exceptions,InnerClasses,Signature,Deprecated,SourceFile,LineNumberTable,*Annotation*,EnclosingMethod
-keepclassmembers class * {
    @android.webkit.JavascriptInterface <methods>;
}
-keep class com.football.ar.JniBridge {
    public <methods>;
}
