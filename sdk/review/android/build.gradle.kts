// The review package's Kotlin for Android: a library module the Asuka
// CLI builds into the apps that import the package (asuka native), with
// Google Play's in-app review library.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "platform.review"
}

dependencies {
    implementation("com.google.android.play:review:2.0.2")
}
