// asuka/lottie for Android: lottie-android, in a library module the
// Asuka CLI builds into the apps that import asuka/lottie.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "asuka.lottie"
}

dependencies {
    implementation("com.airbnb.android:lottie:6.7.1")
}
