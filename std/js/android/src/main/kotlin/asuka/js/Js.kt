// asuka/js on Android: a WebView that's never shown, made for each script
// and destroyed after; the script's value comes back through a
// JavaScript interface, as JSON.
package asuka.js

import android.annotation.SuppressLint
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.webkit.JavascriptInterface
import android.webkit.WebView
import android.webkit.WebViewClient
import dev.asuka.AsukaModules
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlin.time.Duration
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject

class WebEngine(private val context: Context) : Engine {
    @SuppressLint("SetJavaScriptEnabled", "JavascriptInterface")
    override suspend fun run(script: String, timeout: Duration): String =
        withTimeoutOrNull(timeout) {
            withContext(Dispatchers.Main) {
                suspendCancellableCoroutine { cont ->
                    val main = Handler(Looper.getMainLooper())
                    val web = WebView(context)
                    var done = false
                    fun finish(r: Result<String>) {
                        if (done) return
                        done = true
                        // Not while its bridge's call is on the stack.
                        main.post { web.destroy() }
                        if (cont.isActive) r.fold({ cont.resume(it) }, { cont.resumeWithException(it) })
                    }
                    cont.invokeOnCancellation { main.post { finish(Result.failure(it ?: Exception("canceled"))) } }
                    web.settings.javaScriptEnabled = true
                    web.addJavascriptInterface(object {
                        // On the WebView's own thread: posted to the main one.
                        @JavascriptInterface fun done(json: String) { main.post { finish(Result.success(json)) } }
                        @JavascriptInterface fun fail(message: String) { main.post { finish(Result.failure(Exception(message))) } }
                    }, "__asukaScript")
                    web.webViewClient = object : WebViewClient() {
                        override fun onPageFinished(view: WebView, url: String?) {
                            if (done || view.tag == true) return
                            view.tag = true
                            // Indirect eval: the global scope, and the last statement's value.
                            val wrapped = "(function(){var s=" + JSONObject.quote(script) + ";" +
                                "Promise.resolve().then(function(){return (0,eval)(s)}).then(" +
                                "function(v){__asukaScript.done(JSON.stringify(v===undefined?null:v))}," +
                                "function(e){__asukaScript.fail(e&&e.message!==undefined?String(e.message):String(e))})})()"
                            view.evaluateJavascript(wrapped, null)
                        }
                    }
                    web.loadDataWithBaseURL("about:blank", "<!doctype html><html></html>", "text/html", "utf-8", null)
                }
            }
        } ?: throw JsException(JsError.TIMEOUT)
}

fun makeEngine(): Engine = WebEngine(AsukaModules.context)
