// The agerange package's Kotlin for Android: a library module the Asuka
// CLI builds into the apps that import the package (asuka native), with
// Google Play's Age Signals library.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "asuka.agerange"
}

dependencies {
    implementation("com.google.android.play:age-signals:0.0.4")
}
