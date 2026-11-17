// The print package's Kotlin for Android: a library module the Asuka CLI
// builds into the apps that import the package (asuka native).
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "platform.print"
    // AsukaPdfWriter subclasses the print callbacks in their package.
    defaultConfig { consumerProguardFiles("consumer-rules.pro") }
}

dependencies {
    implementation("androidx.exifinterface:exifinterface:1.4.1") // a photo's orientation
}
