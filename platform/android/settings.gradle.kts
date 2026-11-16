// Asuka's Android host: the `asuka` library (decoder, Views host, JNI
// bindings) and `app`, the shell app the Asuka CLI builds for each Asuka app.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    // A native package's module may declare repositories of its own
    // (then its own are its: google() and mavenCentral() too).
    repositoriesMode.set(RepositoriesMode.PREFER_PROJECT)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "asuka-android"
// The host's optional modules, in the apps that need them (the CLI's
// asukaModules): asuka-video (Video's player, Media3) in those that show a
// Video; asuka-push (Firebase's messaging) and asuka-periodic
// (WorkManager), the host's push and periodic modules' libraries, in
// those that import platform/push and platform/periodic.
include(":asuka", ":app", ":asuka-video", ":asuka-push", ":asuka-periodic")

// The app's native packages' modules (their android/, the CLI's
// asukaNativeModules: name=directory, ;-separated).
providers.gradleProperty("asukaNativeModules").orNull?.split(';')?.filter { it.isNotEmpty() }?.forEach {
    val (name, dir) = it.split('=', limit = 2)
    include(":$name")
    project(":$name").projectDir = file(dir)
}
