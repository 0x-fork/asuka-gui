// std/lottie for Android: lottie-android, in a library module the
// Asuka CLI builds into the apps that import std/lottie.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "std.lottie"
}

dependencies {
    implementation("com.airbnb.android:lottie:6.7.1")
}
