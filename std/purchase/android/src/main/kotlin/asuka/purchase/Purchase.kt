// asuka/purchase on Android: the Play Billing Library. Purchases that
// come outside a buy (a pending one paid for, one made in the Play
// Store, one left unacknowledged from before) are kept for events():
// those PurchasesUpdatedListener gives, and those queryPurchasesAsync
// finds unacknowledged as the client connects and as the app comes back
// to the front. Everything touches the client on the main thread.
package asuka.purchase

import android.app.Activity
import android.app.Application
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import com.android.billingclient.api.AcknowledgePurchaseParams
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.ConsumeParams
import com.android.billingclient.api.GetBillingConfigParams
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.ProductDetails
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryProductDetailsParams
import com.android.billingclient.api.QueryPurchasesParams
import com.android.billingclient.api.BillingFlowParams.ProductDetailsParams.SubscriptionProductReplacementParams.ReplacementMode as PlayMode
import dev.asuka.AsukaModules
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

private val main = Handler(Looper.getMainLooper())

private class Failure(m: String) : Exception(m)

class PlayStore(private val app: Application) : Store, PurchasesUpdatedListener {
    private val client: BillingClient = BillingClient.newBuilder(app)
        .setListener(this)
        .enablePendingPurchases(PendingPurchasesParams.newBuilder().enableOneTimeProducts().enablePrepaidPlans().build())
        .enableAutoServiceReconnection()
        .build()
    private var connecting = false
    private val whenConnected = mutableListOf<(BillingResult) -> Unit>()
    /** Products asked for, by ID: launchBillingFlow needs Play's. */
    private val details = HashMap<String, ProductDetails>()
    /** Each product's kind, as the app said. */
    private val kinds = HashMap<String, Kind>()
    /** The purchase under way: its product and buy's continuation. */
    private var buying: Pair<String, (kotlin.Result<StoreResult>) -> Unit>? = null
    /** What each purchase token was last given as (its state), not to give it twice. */
    private val given = HashMap<String, Int>()
    private val kept = ArrayDeque<StoreEvent>()
    private val readers = HashMap<Int, (StoreEvent) -> Unit>()
    private var nextReader = 0
    /** What waits for the app to come back (a store page it opened). */
    private val onReturn = mutableListOf<() -> Unit>()

    init {
        main.post {
            app.registerActivityLifecycleCallbacks(object : Application.ActivityLifecycleCallbacks {
                override fun onActivityResumed(a: Activity) {
                    val fs = onReturn.toList()
                    onReturn.clear()
                    fs.forEach { it() }
                    reconcile()
                }
                override fun onActivityCreated(a: Activity, b: Bundle?) {}
                override fun onActivityStarted(a: Activity) {}
                override fun onActivityPaused(a: Activity) {}
                override fun onActivityStopped(a: Activity) {}
                override fun onActivitySaveInstanceState(a: Activity, b: Bundle) {}
                override fun onActivityDestroyed(a: Activity) {}
            })
            reconcile()
        }
    }

    /** Runs f once the client is connected (or failed to: f gets why). Main thread. */
    private fun connected(f: (BillingResult) -> Unit) {
        if (client.isReady) {
            f(BillingResult.newBuilder().setResponseCode(BillingClient.BillingResponseCode.OK).build())
            return
        }
        whenConnected.add(f)
        if (connecting) return
        connecting = true
        client.startConnection(object : BillingClientStateListener {
            override fun onBillingSetupFinished(r: BillingResult) {
                main.post {
                    connecting = false
                    val fs = whenConnected.toList()
                    whenConnected.clear()
                    fs.forEach { it(r) }
                }
            }
            override fun onBillingServiceDisconnected() {
                main.post { connecting = false } // the client reconnects by itself
            }
        })
    }

    /** body on the main thread once connected; the call fails if the client can't connect. */
    private suspend fun <T> ready(body: (resume: (kotlin.Result<T>) -> Unit) -> Unit): T = suspendCancellableCoroutine { c ->
        main.post {
            connected { r ->
                if (r.responseCode != BillingClient.BillingResponseCode.OK) c.resumeWithException(failure(r))
                else body { res -> if (c.isActive) res.fold({ c.resume(it) }, { c.resumeWithException(it) }) }
            }
        }
    }

    private fun failure(r: BillingResult): Exception = when (r.responseCode) {
        BillingClient.BillingResponseCode.BILLING_UNAVAILABLE, BillingClient.BillingResponseCode.FEATURE_NOT_SUPPORTED -> PurchaseException(PurchaseError.NOT_AVAILABLE)
        BillingClient.BillingResponseCode.ITEM_UNAVAILABLE -> PurchaseException(PurchaseError.UNKNOWN_PRODUCT)
        BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED -> PurchaseException(PurchaseError.ALREADY_OWNED)
        BillingClient.BillingResponseCode.NETWORK_ERROR, BillingClient.BillingResponseCode.SERVICE_UNAVAILABLE -> Failure("purchase: the network: ${r.debugMessage}")
        else -> Failure("purchase: Play Billing (${r.responseCode}): ${r.debugMessage}")
    }

    override fun available(): Boolean = client.isReady || run { main.post { connected {} }; false }

    override fun declare(items: List<Item>) {
        main.post { for (it in items) kinds[it.id] = it.kind }
    }

    private fun playType(k: Kind?) = if (k == Kind.AUTO_RENEWABLE || k == Kind.NON_RENEWING) BillingClient.ProductType.SUBS else BillingClient.ProductType.INAPP

    /** Asks Play for products, one query per type; done gets those it doesn't sell. Main thread. */
    private fun query(items: List<Item>, done: (kotlin.Result<List<String>>) -> Unit) {
        val byType = items.groupBy { playType(it.kind) }
        var left = byType.size
        if (left == 0) return done(kotlin.Result.success(emptyList()))
        val missing = mutableListOf<String>()
        var failed: Exception? = null
        for ((type, its) in byType) {
            val ps = its.map { QueryProductDetailsParams.Product.newBuilder().setProductId(it.id).setProductType(type).build() }
            client.queryProductDetailsAsync(QueryProductDetailsParams.newBuilder().setProductList(ps).build()) { r, res ->
                main.post {
                    if (r.responseCode != BillingClient.BillingResponseCode.OK) failed = failure(r)
                    else {
                        res.productDetailsList.forEach { details[it.productId] = it }
                        res.unfetchedProductList.forEach { missing.add(it.productId) }
                    }
                    if (--left == 0) {
                        val f = failed
                        done(if (f != null) kotlin.Result.failure(f) else kotlin.Result.success(missing))
                    }
                }
            }
        }
    }

    override suspend fun products(items: List<Item>): List<Product> = ready { resume ->
        for (it in items) kinds[it.id] = it.kind
        query(items) { res -> resume(res.map { items.mapNotNull { i -> details[i.id]?.let { d -> wire(d) } } }) }
    }

    override suspend fun buy(item: Item, options: StoreOptions): StoreResult = ready { resume ->
        kinds[item.id] = item.kind
        val go = go@{
            val p = details[item.id] ?: return@go resume(kotlin.Result.failure(PurchaseException(PurchaseError.UNKNOWN_PRODUCT)))
            val act = AsukaModules.activity ?: return@go resume(kotlin.Result.failure(Failure("purchase: no screen")))
            if (buying != null) return@go resume(kotlin.Result.failure(Failure("purchase: a purchase is under way")))
            val pp = BillingFlowParams.ProductDetailsParams.newBuilder().setProductDetails(p)
            val token = options.offer?.token ?: ""
            if (token.isNotEmpty()) pp.setOfferToken(token)
            else p.subscriptionOfferDetails?.let { offers ->
                // A subscription needs an offer: the first base plan's.
                (offers.firstOrNull { it.offerId == null } ?: offers.firstOrNull())?.let { pp.setOfferToken(it.offerToken) }
            }
            val flow = BillingFlowParams.newBuilder()
            options.replace?.let { r ->
                pp.setSubscriptionProductReplacementParams(
                    BillingFlowParams.ProductDetailsParams.SubscriptionProductReplacementParams.newBuilder()
                        .setOldProductId(r.old.productId)
                        .setReplacementMode(replacementMode(r.mode))
                        .build(),
                )
                flow.setSubscriptionUpdateParams(BillingFlowParams.SubscriptionUpdateParams.newBuilder().setOldPurchaseToken(r.old.verification.token).build())
            }
            flow.setProductDetailsParamsList(listOf(pp.build()))
            if (options.accountToken.isNotEmpty()) flow.setObfuscatedAccountId(options.accountToken)
            if (options.profileToken.isNotEmpty()) flow.setObfuscatedProfileId(options.profileToken)
            if (options.personalized) flow.setIsOfferPersonalized(true)
            buying = item.id to resume
            val r = client.launchBillingFlow(act, flow.build())
            if (r.responseCode != BillingClient.BillingResponseCode.OK) {
                buying = null
                resume(kotlin.Result.failure(failure(r)))
            }
        }
        if (details[item.id] != null) go()
        else query(listOf(item)) { res -> res.fold({ go() }, { resume(kotlin.Result.failure(it)) }) }
    }

    private fun replacementMode(m: ReplaceMode): Int = when (m) {
        ReplaceMode.CHARGE_PRORATED_PRICE -> PlayMode.CHARGE_PRORATED_PRICE
        ReplaceMode.CHARGE_FULL_PRICE -> PlayMode.CHARGE_FULL_PRICE
        ReplaceMode.WITHOUT_PRORATION -> PlayMode.WITHOUT_PRORATION
        ReplaceMode.DEFERRED -> PlayMode.DEFERRED
        else -> PlayMode.WITH_TIME_PRORATION
    }

    override fun onPurchasesUpdated(r: BillingResult, purchases: MutableList<Purchase>?) {
        val ps = purchases?.toList() ?: emptyList()
        main.post { updated(r, ps) }
    }

    private fun updated(r: BillingResult, purchases: List<Purchase>) {
        val b = buying
        var mine: Purchase? = null
        if (b != null) {
            buying = null
            val (id, resume) = b
            when (r.responseCode) {
                BillingClient.BillingResponseCode.OK -> {
                    mine = purchases.firstOrNull { id in it.products } ?: purchases.firstOrNull()
                    val p = mine
                    if (p == null) resume(kotlin.Result.success(StoreResult(status = Status.CANCELLED)))
                    else {
                        given[p.purchaseToken] = p.purchaseState
                        val bought = p.purchaseState == Purchase.PurchaseState.PURCHASED
                        resume(kotlin.Result.success(StoreResult(status = if (bought) Status.BOUGHT else Status.PENDING_APPROVAL, transaction = wire(p))))
                    }
                }
                BillingClient.BillingResponseCode.USER_CANCELED -> resume(kotlin.Result.success(StoreResult(status = Status.CANCELLED)))
                else -> resume(kotlin.Result.failure(failure(r)))
            }
        }
        if (r.responseCode == BillingClient.BillingResponseCode.OK) {
            for (p in purchases) if (p !== mine) give(p)
        }
    }

    /** Gives a purchase to events(), unless it was given as it is. */
    private fun give(p: Purchase) {
        if (given[p.purchaseToken] == p.purchaseState) return
        given[p.purchaseToken] = p.purchaseState
        val e = StoreEvent(type = EventType.TRANSACTION_UPDATED, transaction = wire(p))
        if (readers.isEmpty()) kept.addLast(e) else readers.values.toList().forEach { it(e) }
    }

    /** Both types' purchases. Main thread. */
    private fun purchases(suspended: Boolean, done: (kotlin.Result<List<Purchase>>) -> Unit) {
        val out = mutableListOf<Purchase>()
        var left = 2
        var failed: Exception? = null
        for (type in listOf(BillingClient.ProductType.INAPP, BillingClient.ProductType.SUBS)) {
            val params = QueryPurchasesParams.newBuilder().setProductType(type)
            if (suspended && type == BillingClient.ProductType.SUBS) params.includeSuspendedSubscriptions(true)
            client.queryPurchasesAsync(params.build()) { r, ps ->
                main.post {
                    if (r.responseCode != BillingClient.BillingResponseCode.OK) failed = failure(r)
                    else {
                        for (p in ps) p.products.forEach { id ->
                            kinds.putIfAbsent(id, if (type == BillingClient.ProductType.SUBS) Kind.AUTO_RENEWABLE else Kind.NON_CONSUMABLE)
                        }
                        out.addAll(ps)
                    }
                    if (--left == 0) {
                        val f = failed
                        done(if (f != null) kotlin.Result.failure(f) else kotlin.Result.success(out))
                    }
                }
            }
        }
    }

    /** Gives the purchases paid for but not acknowledged: from before, outside the app, a pending one paid. */
    private fun reconcile() = connected { r ->
        if (r.responseCode != BillingClient.BillingResponseCode.OK) return@connected
        purchases(false) { res ->
            res.getOrNull()?.forEach { if (it.purchaseState == Purchase.PurchaseState.PURCHASED && !it.isAcknowledged) give(it) }
        }
    }

    override suspend fun entitlements(): List<StoreTransaction> = ready { resume ->
        purchases(false) { res -> resume(res.map { ps -> ps.filter { it.purchaseState == Purchase.PurchaseState.PURCHASED }.map { wire(it) } }) }
    }

    override suspend fun restore(): List<StoreTransaction> {
        withContext(Dispatchers.Main) { reconcile() }
        return entitlements()
    }

    override suspend fun statuses(group: String): List<StoreStatus> = ready { resume ->
        purchases(true) { res ->
            resume(res.map { ps ->
                ps.filter { group in it.products && it.purchaseState == Purchase.PurchaseState.PURCHASED && playType(kinds[group] ?: Kind.AUTO_RENEWABLE) == BillingClient.ProductType.SUBS }
                    .map { p ->
                        StoreStatus(productId = group, state = if (p.isSuspended) SubscriptionState.BILLING_RETRY else SubscriptionState.SUBSCRIBED,
                            willRenew = p.isAutoRenewing, transaction = wire(p))
                    }
            })
        }
    }

    override suspend fun manageSubscriptions(productId: String) {
        var u = "https://play.google.com/store/account/subscriptions"
        if (productId.isNotEmpty()) u += "?sku=" + Uri.encode(productId) + "&package=" + Uri.encode(app.packageName)
        open(u)
    }

    override suspend fun requestRefund(transactionId: String): Boolean = throw PurchaseException(PurchaseError.NOT_AVAILABLE)

    override suspend fun redeemCode() = open("https://play.google.com/redeem")

    /** Opens a Play page, answering as the app comes back to the front. */
    private suspend fun open(url: String) = suspendCancellableCoroutine { c ->
        main.post {
            val act = AsukaModules.activity
            if (act == null) {
                c.resumeWithException(Failure("purchase: no screen"))
                return@post
            }
            try {
                act.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
                onReturn.add { if (c.isActive) c.resume(Unit) }
            } catch (e: Exception) {
                c.resumeWithException(e)
            }
        }
    }

    override suspend fun storefront(): Storefront = ready { resume ->
        client.getBillingConfigAsync(GetBillingConfigParams.newBuilder().build()) { r, cfg ->
            main.post {
                if (r.responseCode != BillingClient.BillingResponseCode.OK || cfg == null) resume(kotlin.Result.failure(failure(r)))
                else resume(kotlin.Result.success(Storefront(country = cfg.countryCode)))
            }
        }
    }

    override suspend fun events(event: (StoreEvent) -> Unit) {
        val id = withContext(Dispatchers.Main) {
            val n = ++nextReader
            readers[n] = event
            while (kept.isNotEmpty()) event(kept.removeFirst())
            n
        }
        try {
            awaitCancellation()
        } finally {
            main.post { readers.remove(id) }
        }
    }

    override suspend fun finish(transaction: StoreTransaction) = ready { resume ->
        val token = transaction.verification.token
        if (token.isEmpty()) return@ready resume(kotlin.Result.failure(Failure("purchase: no purchase token")))
        val consume = (kinds[transaction.productId] ?: transaction.kind) == Kind.CONSUMABLE
        val done = { r: BillingResult ->
            main.post {
                if (r.responseCode == BillingClient.BillingResponseCode.OK || r.responseCode == BillingClient.BillingResponseCode.ITEM_NOT_OWNED && !consume) {
                    resume(kotlin.Result.success(Unit))
                } else resume(kotlin.Result.failure(failure(r)))
            }
        }
        if (consume) client.consumeAsync(ConsumeParams.newBuilder().setPurchaseToken(token).build()) { r, _ -> done(r) }
        else client.acknowledgePurchase(AcknowledgePurchaseParams.newBuilder().setPurchaseToken(token).build()) { r -> done(r) }
    }

    // The package's types.

    private fun period(iso: String): Period {
        // ISO 8601: P1W, P1M, P3M, P1Y.
        val m = Regex("P(\\d+)([DWMY])").find(iso)
        val n = m?.groupValues?.get(1)?.toLong() ?: 0L
        val unit = when (m?.groupValues?.get(2)) { "D" -> PeriodUnit.DAY; "W" -> PeriodUnit.WEEK; "M" -> PeriodUnit.MONTH; else -> PeriodUnit.YEAR }
        return Period(unit = unit, count = n)
    }

    private fun phases(ps: List<ProductDetails.PricingPhase>): List<Phase> = ps.map { ph ->
        val mode = when (ph.recurrenceMode) {
            ProductDetails.RecurrenceMode.INFINITE_RECURRING -> Mode.RECURRING
            ProductDetails.RecurrenceMode.FINITE_RECURRING -> if (ph.priceAmountMicros == 0L) Mode.FREE_TRIAL else Mode.PAY_AS_YOU_GO
            else -> if (ph.priceAmountMicros == 0L) Mode.FREE_TRIAL else Mode.PAY_UP_FRONT
        }
        Phase(price = ph.formattedPrice, priceMicros = ph.priceAmountMicros, currency = ph.priceCurrencyCode,
            period = period(ph.billingPeriod), cycles = maxOf(ph.billingCycleCount, 1).toLong(), mode = mode)
    }

    private fun wire(p: ProductDetails): Product {
        val kind = kinds[p.productId] ?: if (p.productType == BillingClient.ProductType.SUBS) Kind.AUTO_RENEWABLE else Kind.NON_CONSUMABLE
        val subs = p.subscriptionOfferDetails
        if (subs != null) {
            val base = subs.firstOrNull { it.offerId == null } ?: subs.firstOrNull()
            val last = base?.pricingPhases?.pricingPhaseList?.lastOrNull()
            val offers = subs.map { s ->
                Offer(id = s.offerId ?: "", type = if (s.offerId == null) OfferType.BASE else OfferType.OFFER, basePlan = s.basePlanId,
                    tags = s.offerTags, phases = phases(s.pricingPhases.pricingPhaseList), token = s.offerToken)
            }
            return Product(id = p.productId, kind = kind, title = p.name, description = p.description,
                price = last?.formattedPrice ?: "", priceMicros = last?.priceAmountMicros ?: 0, currency = last?.priceCurrencyCode ?: "",
                subscription = Subscription(group = p.productId, period = last?.let { period(it.billingPeriod) } ?: Period(),
                    offers = offers, introEligible = subs.any { it.offerId != null && it.pricingPhases.pricingPhaseList.size > 1 }))
        }
        val one = p.oneTimePurchaseOfferDetails
        val offers = p.oneTimePurchaseOfferDetailsList?.map { d ->
            Offer(id = d.offerId ?: "", type = OfferType.ONE_TIME, basePlan = d.purchaseOptionId ?: "", tags = d.offerTags ?: emptyList(), token = d.offerToken ?: "",
                phases = listOf(Phase(price = d.formattedPrice, priceMicros = d.priceAmountMicros, currency = d.priceCurrencyCode, cycles = 1, mode = Mode.PAY_UP_FRONT)))
        } ?: emptyList()
        return Product(id = p.productId, kind = kind, title = p.name, description = p.description,
            price = one?.formattedPrice ?: "", priceMicros = one?.priceAmountMicros ?: 0, currency = one?.priceCurrencyCode ?: "", offers = offers)
    }

    private fun wire(p: Purchase): StoreTransaction {
        val id = p.products.firstOrNull() ?: ""
        val a = p.accountIdentifiers
        return StoreTransaction(
            id = p.orderId ?: p.purchaseToken, productId = id, kind = kinds[id] ?: Kind.NON_CONSUMABLE, quantity = p.quantity.toLong(),
            state = if (p.purchaseState == Purchase.PurchaseState.PURCHASED) State.PURCHASED else State.PENDING,
            date = p.purchaseTime * 1_000_000, autoRenews = p.isAutoRenewing, acknowledged = p.isAcknowledged,
            accountToken = a?.obfuscatedAccountId ?: "", profileToken = a?.obfuscatedProfileId ?: "",
            verification = Verification(token = p.purchaseToken, packageName = p.packageName, originalJSON = p.originalJson, signature = p.signature),
        )
    }
}

fun makeStore(): Store = PlayStore(AsukaModules.context.applicationContext as Application)
