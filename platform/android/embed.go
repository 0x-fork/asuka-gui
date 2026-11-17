// Package android embeds the Android host's Gradle project for the Asuka
// CLI, which builds the shell app from it.
package android

import "embed"

// Files holds the Asuka library, its optional modules (asuka-video,
// asuka-push, asuka-periodic), the shell app and the Gradle wrapper
// (gradlew, and gradlew.bat for Windows).
//
//go:embed settings.gradle.kts build.gradle.kts gradle.properties gradlew gradlew.bat gradle/wrapper asuka/build.gradle.kts asuka/consumer-rules.pro asuka/src asuka-video/build.gradle.kts asuka-video/consumer-rules.pro asuka-video/src asuka-push/build.gradle.kts asuka-push/src asuka-periodic/build.gradle.kts asuka-periodic/src app/build.gradle.kts app/src
var Files embed.FS
