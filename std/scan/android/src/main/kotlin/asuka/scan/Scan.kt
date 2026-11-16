// asuka/scan on Android: ML Kit's barcode reader, with its model.
package asuka.scan

import android.net.Uri
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import dev.asuka.AsukaModules
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

private val formats = mapOf(
    "qr" to Barcode.FORMAT_QR_CODE, "aztec" to Barcode.FORMAT_AZTEC,
    "datamatrix" to Barcode.FORMAT_DATA_MATRIX, "pdf417" to Barcode.FORMAT_PDF417,
    "ean13" to Barcode.FORMAT_EAN_13, "ean8" to Barcode.FORMAT_EAN_8,
    "upca" to Barcode.FORMAT_UPC_A, "upce" to Barcode.FORMAT_UPC_E,
    "code128" to Barcode.FORMAT_CODE_128, "code39" to Barcode.FORMAT_CODE_39,
    "code93" to Barcode.FORMAT_CODE_93, "codabar" to Barcode.FORMAT_CODABAR,
    "itf" to Barcode.FORMAT_ITF,
)
private val kindOf = formats.entries.associate { (k, v) -> v to k }

private class MLKitReader : Reader {
    override suspend fun file(path: String, kinds: List<String>): List<Code> = withContext(Dispatchers.IO) {
        val img = try {
            InputImage.fromFilePath(AsukaModules.context, Uri.fromFile(File(path.removePrefix("file://"))))
        } catch (e: Exception) {
            throw Exception("can't read the image")
        }
        val fs = kinds.mapNotNull { formats[it] }.ifEmpty { listOf(Barcode.FORMAT_ALL_FORMATS) }
        val c = BarcodeScanning.getClient(BarcodeScannerOptions.Builder().setBarcodeFormats(fs[0], *fs.drop(1).toIntArray()).build())
        try {
            Tasks.await(c.process(img)).mapNotNull { b ->
                val k = kindOf[b.format]
                val v = b.rawValue
                if (k == null || v == null) null else Code(kind = k, value = v)
            }.distinct()
        } finally {
            c.close()
        }
    }
}

fun makeReader(): Reader = MLKitReader()
