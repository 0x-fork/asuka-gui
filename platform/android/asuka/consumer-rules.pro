# The Go core calls these through JNI.
-keep class dev.asuka.AsukaRuntime { *; }
-keep class dev.asuka.AsukaNative { *; }
# Packages' typed modules (asuka native's AsukaModule objects), which the
# host finds by name, and the runtime JNI calls.
-keep class * implements dev.asuka.AsukaModuleProvider { *; }
-keep class dev.asuka.AsukaModules { *; }
# The optional modules' plugins (asuka-push, asuka-periodic), found by name.
-keep class * implements dev.asuka.AsukaPlugin { <init>(); }
