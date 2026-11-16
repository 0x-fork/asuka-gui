package dev.asuka

import org.junit.Assert.assertEquals
import org.junit.Test

// The C core names a package's native code by its path; the Kotlin class
// it's found by is internal/nativegen's KotlinClass of that path.
class ModuleClassTest {
    @Test fun classOfPath() {
        assertEquals("platform.network.AsukaModule", AsukaModules.kotlinClass("platform/network"))
        assertEquals("_asuka.segmented.AsukaModule", AsukaModules.kotlinClass("@asuka/segmented"))
        assertEquals("com.github.acme.go_battery.AsukaModule", AsukaModules.kotlinClass("github.com/acme/go-battery"))
        assertEquals("com.example.a_b.c.AsukaModule", AsukaModules.kotlinClass("example.com/a-b/c"))
        assertEquals("segmented.AsukaModule", AsukaModules.kotlinClass("segmented"))
    }
}
