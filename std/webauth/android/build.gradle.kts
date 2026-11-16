// The webauth package's Kotlin for Android: a library module the Asuka
// CLI builds into the apps that import the package (asuka native), with
// AndroidX's Custom Tabs.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "asuka.webauth"
}

dependencies {
    implementation("androidx.browser:browser:1.9.0")
}
