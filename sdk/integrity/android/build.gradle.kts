// The integrity package's Kotlin for Android: a library module the Asuka
// CLI builds into the apps that import the package (asuka native), with
// Google Play's Integrity library.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "platform.integrity"
}

dependencies {
    implementation("com.google.android.play:integrity:1.6.0")
}
