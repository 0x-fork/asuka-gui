// The purchase package's Kotlin for Android: a library module the Asuka
// CLI builds into the apps that import the package (asuka native), with
// Google Play Billing (its manifest declares com.android.vending.BILLING).
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "asuka.purchase"
}

dependencies {
    implementation("com.android.billingclient:billing:9.1.0")
}
