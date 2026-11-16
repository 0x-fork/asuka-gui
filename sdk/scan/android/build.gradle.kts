// The scan package's Kotlin for Android: a library module the Asuka
// CLI builds into the apps that import the package (asuka native). Its
// Kotlin is in the package's namespace; its dependencies go here.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "platform.scan"
}

dependencies {
    // ML Kit's barcode reader, with its model (about 2 MB), so it works
    // without Google Play services.
    implementation("com.google.mlkit:barcode-scanning:17.3.0")
}
