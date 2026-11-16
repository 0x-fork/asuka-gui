// platform/secure on Android: each value sealed (AES-GCM, its IV first)
// with a key the Android Keystore keeps and never lets out, then kept,
// base64, in the app's private preferences. The key is the host's own
// ("asuka.secure"), so values its secure store kept are read here.
package platform.secure

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import dev.asuka.AsukaModules
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class KeystoreKeychain(context: Context) : Keychain {
    private val prefs = context.getSharedPreferences("asuka.secure", Context.MODE_PRIVATE)

    override suspend fun get(key: String): String? = withContext(Dispatchers.IO) {
        val v = prefs.getString(key, null) ?: return@withContext null
        val raw = Base64.decode(v, Base64.NO_WRAP)
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.DECRYPT_MODE, secretKey(), GCMParameterSpec(128, raw, 0, 12))
        String(c.doFinal(raw, 12, raw.size - 12), Charsets.UTF_8)
    }

    override suspend fun set(key: String, value: String) = withContext(Dispatchers.IO) {
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, secretKey())
        val sealed = c.iv + c.doFinal(value.toByteArray(Charsets.UTF_8))
        if (!prefs.edit().putString(key, Base64.encodeToString(sealed, Base64.NO_WRAP)).commit()) {
            throw java.io.IOException("secure: couldn't write the value")
        }
    }

    override suspend fun remove(key: String) = withContext(Dispatchers.IO) {
        prefs.edit().remove(key).commit()
        kotlin.Unit
    }

    /** The key, in the Android Keystore: made the first time. */
    @Synchronized
    private fun secretKey(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getKey("asuka.secure", null) as? SecretKey)?.let { return it }
        val g = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        g.init(KeyGenParameterSpec.Builder("asuka.secure", KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .build())
        return g.generateKey()
    }
}

fun makeKeychain(): Keychain = KeystoreKeychain(AsukaModules.context)
