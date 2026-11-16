// The biometric package's Kotlin for Android: a library module the Asuka
// CLI builds into the apps that import the package (asuka native), with
// AndroidX's BiometricPrompt.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "platform.biometric"
}

dependencies {
    implementation("androidx.biometric:biometric:1.1.0")
}
