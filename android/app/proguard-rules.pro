# Registered JNI entry points and JSON field names form the stable native ABI.
-keep class com.monsterengine.viewer.NativeBridge { *; }
-keepclasseswithmembernames,includedescriptorclasses class * { native <methods>; }
-keepattributes Signature,InnerClasses,EnclosingMethod
