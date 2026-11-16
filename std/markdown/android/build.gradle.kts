// The markdown package's Kotlin for Android: a library module the Asuka
// CLI builds into the apps that import the package (asuka native). Its
// Kotlin is in the package's namespace; its dependencies go here.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "asuka.markdown"
}

dependencies {
    // Markwon: CommonMark (commonmark-java) as Android's spans.
    implementation("io.noties.markwon:core:4.6.2")
    implementation("io.noties.markwon:ext-strikethrough:4.6.2")
    implementation("io.noties.markwon:ext-tables:4.6.2")
    implementation("io.noties.markwon:ext-tasklist:4.6.2")
    implementation("io.noties.markwon:linkify:4.6.2")
}
