// The pay package's Kotlin for Android: a library module the Asuka CLI
// builds into the apps that import the package (asuka native), with
// Google Pay (play-services-wallet: its API enabled in the module's
// manifest).
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "platform.pay"
}

dependencies {
    implementation("com.google.android.gms:play-services-wallet:20.0.0")
}
