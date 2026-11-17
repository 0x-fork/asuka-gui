// The preview package's Kotlin for Android: a library module the Asuka
// CLI builds into the apps that import the package (asuka native).
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "platform.preview"
}

dependencies {
    implementation("androidx.activity:activity:1.10.1")
    implementation("androidx.core:core-ktx:1.16.0")
}
