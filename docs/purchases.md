# In-app purchases

`asuka/purchase` sells in the app, through each platform's own store:
consumables (coins, a hint), non-consumables (a level, "Pro" for life),
subscriptions that renew, and those that don't. It covers what
react-native-iap, expo-iap and RevenueCat's SDK do on the device, with
the stores' current APIs:

| Platform | Store | API |
| --- | --- | --- |
| iOS, macOS | the App Store | StoreKit 2 |
| Android | Google Play | the Play Billing Library 9 |
| Windows | the Microsoft Store | `Windows.Services.Store`, for an app installed from the Store |
| The web | Google Play | the Digital Goods API and Payment Request, in a Trusted Web Activity |
| Linux | none | |

Where there's no store (Linux, a browser without the Digital Goods API, a
Windows app that wasn't installed from the Store, the player in
`asuka dev`), `purchase.available()` is false and the rest throw
`PurchaseError.notAvailable`.

## Products

An `Item` is a product the app sells: its ID in the store and its kind
(`.consumable`, `.nonConsumable`, `.autoRenewable`, `.nonRenewing`).
Play doesn't know a consumable from a non-consumable, so the app says.

```asuka
import asuka/purchase

let coins = purchase.Item(id: "coins_100", kind: .consumable)
let pro = purchase.Item(id: "pro", kind: .nonConsumable)
let plus = purchase.Item(id: "plus_monthly", kind: .autoRenewable)
```

`purchase.declare(items)` says what the app sells without asking the
store; call it as the app starts, so a consumable's transaction that comes
first (one left unfinished) is consumed as it's finished.
`purchase.products(items)` asks the store for them, in the user's country
and language; those it doesn't sell are left out. A `Product` has its
`title`, `description`, `price` (for display: "$4.99"), `priceMicros`
and `currency`, `item()` (to buy it as), and a subscription's
`subscription`: its `group`, `level`, `period` (`unit`, `count`), its
`offers` (each a list of `phases`: a free trial, a lower price for a
while) and whether the user can have the introductory offer
(`introEligible`).

```asuka
component Shop {
  state products: [purchase.Product] = []

  task {
    purchase.declare([coins, pro, plus])
    products = (try? await purchase.products([coins, pro, plus])) ?? []
  }

  Column {
    for p in products key p.id {
      Text("{p.title} {p.price}")
    }
  }
}
```

## Buying

`purchase.buy(item, options)` shows the store's sheet and answers once
the user has bought, cancelled, or it waits for approval. Its `Result` has
a `status`: `.bought` (with its `transaction`), `.pendingApproval` (Ask to
Buy, a payment Play waits for: the transaction comes later, through
`events()`) or `.cancelled`.

```asuka
async fn buyCoins() throws {
  let r = try await purchase.buy(coins)
  if r.status == .bought {
    if let t = r.transaction { await deliver(t) }
  }
}
```

`Options` has a consumable's `quantity`, an `accountToken` that ties the
purchase to the app's user (a UUID: Apple's must be one), Play's
`profileToken`, an `offer` to buy a subscription with (Play needs one:
without, the first base plan), an Apple promotional offer's `signature`
(from the app's server), Play's `replace` (the subscription this one
replaces, and how) and `personalized`. It throws `notAllowed` (payments
turned off), `unknownProduct`, `alreadyOwned`.

## Transactions

`purchase.events()` is what the store says outside a buy: a pending
purchase paid for, a purchase made outside the app (on another device, an
offer code redeemed in the store), a subscription renewed, a refund
(`.revoked`), those left unfinished from before, and storefront changes.
What came before anything read it, from the app's start, is kept for the
first reader: read it as the app starts.

Deliver a transaction's content, then `finish()` it. Until then the store
gives it again (at each start): StoreKit keeps it unfinished; Play refunds
a purchase not finished within three days. Finishing a consumable
consumes it, so it can be bought again; a `.pending` one isn't finished.

```asuka
state balance = 0

async fn deliver(t: purchase.Transaction) {
  if t.state == .pending {
    return
  }
  if t.state == .purchased && t.productId == coins.id {
    balance += 100 * t.quantity
  }
  try? await t.finish()
}

async fn followStore() throws {
  for await e in purchase.events() {
    if let t = e.transaction { await deliver(t) }
  }
}
```

A `Transaction` has its `id`, `originalId` (a subscription's renewals
share it), `productId`, `kind`, `state`, `quantity`, `date` and
`expires` (nanoseconds since 1970), `revoked` and `revocationReason`,
`autoRenews`, `familyShared`, the buy's `accountToken`, Apple's
`environment`, the `storefront` it was bought in, its `offerId` and
`offerType`, and its `verification`.

## What the user owns, and restoring

`purchase.owned()` is what the user owns now (non-consumables,
subscriptions in their period, non-renewing ones), tracked: read in a
binding, it's asked for as it's first read, and again after each buy,
finish and restore and each transaction, while it's read.
`owned.has(productId)` says whether one is the product's.

```asuka
component Unlock {
  let owned = purchase.owned()

  Column {
    if owned.has(pro.id) {
      Text("Pro: every level")
    } else {
      Button("Restore Purchases") { on press { let _ = try? await purchase.restore() } }
    }
  }
}
```

`purchase.restore()` asks the store for the user's purchases again (iOS's
`AppStore.sync`, which asks the user to sign in, so only from a button),
and answers what they own. `purchase.entitlements()` answers it without
asking.

## Subscriptions

`purchase.statuses(group)` is the status of a group's subscriptions
(Apple's subscription group; on Play, the subscription's product): its
`state` (`.subscribed`, `.expired`, `.billingRetry`, `.gracePeriod`,
`.revoked`), whether it `willRenew` and as what (`renewsTo`), its
`renewal`, `graceExpires`, `expirationReason` and Apple's signed
`renewalJWS`. `purchase.manageSubscriptions(productId)` shows the store's
page of the user's subscriptions (iOS's sheet, Play's subscriptions
center, the App Store's or the Microsoft Store's account page).
`purchase.redeemCode()` shows the offer code sheet (Apple's) or Play's
redeem page; what it bought comes through `events()`.
`purchase.requestRefund(t)` shows Apple's refund sheet (Apple's alone).
`purchase.storefront()` is the store's country.

## Verifying on a server

A server checks a purchase with the store before it grants what was
sold. A transaction's `verification` has what each store's API takes:

- Apple: `jws`, the transaction signed by the App Store. Verify it with
  the App Store Server Library (its `SignedDataVerifier`). `verified`
  says whether StoreKit already checked it on the device.
- Google Play: `token`, the purchase token, with `packageName`, for the
  Google Play Developer API (`purchases.productsv2`,
  `purchases.subscriptionsv2`). `originalJSON` and `signature` are the
  purchase signed with the app's license key.
- Windows: `token`, the add-on's Store ID, for the Microsoft Store
  collections API.

## Testing your purchases

The stores sell only to an app they know. On iOS, `asuka dev` builds an
app that imports `asuka/purchase` as itself, without hot reload's
player, so a save reinstalls it. On Android, Play sells only to a build
installed from one of its tracks.

**The iOS simulator.** Make a StoreKit configuration file in Xcode (File ›
New › File › StoreKit Configuration File; or sync it from App Store
Connect), with the products, and name it in `asuka.toml`:

```toml
[ios]
storekit = "Products.storekit"
```

`asuka dev`'s build for the simulator carries it, and as `asuka dev`
launches it, sells from it, as an app run from Xcode with the file in its
scheme does: prices, Ask to Buy, failed transactions and faster renewals
come from the file. Other builds buy from the App Store's sandbox: sign in
with a sandbox account, or install a TestFlight build.

**Android.** Upload a build to the internal track (`asuka publish
--platform android --track internal`), add the products in Play
Console, add your Google account to the license testers, and install the
build from the track's link. License testers are never charged.

**Windows.** Associate the app with the Store in Partner Center and
install it from there (a package flight for testing).

**In tests.** `purchase.fake(products)` gives the package a `FakeStore`
that sells `FakeProduct`s and keeps the transactions as StoreKit does:
unfinished until the app finishes them, given again (`relaunch()`) until
then.

```asuka
test "Ask to Buy, then approved" {
  let store = purchase.fake([
    purchase.FakeProduct(id: "coins_100", kind: .consumable, title: "100 Coins", price: "$0.99", priceMicros: 990_000),
    purchase.FakeProduct(id: "plus_monthly", kind: .autoRenewable, title: "Plus", price: "$4.99",
      period: purchase.Period(unit: .month), trial: purchase.Period(unit: .week)),
  ])
  store.outcome = .pending
  let r = try await purchase.buy(coins)
  expect(r.status == .pendingApproval)
  let _ = store.approve("coins_100")   // the parent approves: events() gives it
}
```

`outcome` is `.bought`, `.pending`, `.cancelled` or `.failed`;
`unavailable` and `notAllowed` are a store that isn't there and payments
turned off; `deliver(id)` is a purchase on another device, `renew(id)` a
renewal, `expire(id)` a subscription that ended, `refund(id)` a refund,
`setCountry(c)` a storefront change; `unfinished()` lists what the app
hasn't finished, `transactions` everything, `restores`, `manages`,
`redeems` and `refunds` the calls made.

## Setting up the app

- **iOS and macOS.** The In-App Purchase capability is on for every App
  ID, and it needs no entitlement. Agree to the Paid Apps Agreement in
  App Store Connect, and add the products there. The Mac's StoreKit 2
  needs macOS 12: before, `available()` is false.
- **Android.** An app that imports `asuka/purchase` is built with Play
  Billing and the `com.android.vending.BILLING` permission; others
  aren't.
- **Windows.** Add the add-ons in Partner Center, to an app that's
  packaged (MSIX) and associated with the Store.
- **The web.** Package the site as a Trusted Web Activity with Play
  Billing enabled (Bubblewrap's `playBilling`), and add the products in
  Play Console. Acknowledge a non-consumable from your server, with the
  Play Developer API, within three days.
