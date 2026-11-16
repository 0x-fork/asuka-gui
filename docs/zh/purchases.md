# 应用内购买

`asuka/purchase` 通过各平台自己的应用商店，在应用里售卖商品：消耗型商品（金币、提示）、非消耗型商品（一个关卡、永久的“Pro”）、自动续期的订阅，以及不自动续期的订阅。它覆盖了 react-native-iap、expo-iap 和 RevenueCat 的 SDK 在设备端提供的能力，用的都是各商店当前的 API：

| 平台 | 应用商店 | API |
| --- | --- | --- |
| iOS、macOS | App Store | StoreKit 2 |
| Android | Google Play | Play Billing Library 9 |
| Windows | Microsoft Store | `Windows.Services.Store`，仅限从商店安装的应用 |
| Web | Google Play | Trusted Web Activity 中的 Digital Goods API 和 Payment Request |
| Linux | 无 | |

在没有应用商店的地方（Linux、不支持 Digital Goods API 的浏览器、不是从商店安装的 Windows 应用），`purchase.available()` 为 false，其余调用都会抛出 `PurchaseError.notAvailable`。

## 商品

`Item` 是应用售卖的一件商品：它在商店里的 ID 和它的类型（`.consumable`、`.nonConsumable`、`.autoRenewable`、`.nonRenewing`）。Play 分不清消耗型和非消耗型商品，所以要由应用说明。

```asuka
import asuka/purchase

let coins = purchase.Item(id: "coins_100", kind: .consumable)
let pro = purchase.Item(id: "pro", kind: .nonConsumable)
let plus = purchase.Item(id: "plus_monthly", kind: .autoRenewable)
```

`purchase.declare(items)` 声明应用售卖哪些商品，但不会去询问商店。请在应用启动时调用它，这样最先到来的消耗型商品交易（上次没有完成的那笔）在完成时就会被消耗掉。`purchase.products(items)` 向商店查询这些商品，使用用户所在的国家和语言；商店不卖的会被略去。每个 `Product` 有 `title`、`description`、用于显示的 `price`（“$4.99”）、`priceMicros` 和 `currency`、`item()`（购买时用它），订阅还有 `subscription`：它的 `group`、`level`、`period`（`unit`、`count`）、`offers`（每个优惠是一组 `phases`：免费试用、一段时间内的优惠价），以及用户是否还能享受推介优惠（`introEligible`）。

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

## 购买

`purchase.buy(item, options)` 弹出商店的购买界面，等到用户完成购买、取消，或者购买进入等待批准状态时返回。返回的 `Result` 有一个 `status`：`.bought`（附带它的 `transaction`）、`.pendingApproval`（“询问购买”，或 Play 正在等待的付款：交易稍后通过 `events()` 送达）或 `.cancelled`。

```asuka
async fn buyCoins() throws {
  let r = try await purchase.buy(coins)
  if r.status == .bought {
    if let t = r.transaction { await deliver(t) }
  }
}
```

`Options` 包括：消耗型商品的 `quantity`；把这次购买和应用里的用户关联起来的 `accountToken`（一个 UUID：Apple 要求必须是 UUID）；Play 的 `profileToken`；购买订阅时使用的 `offer`（Play 必须指定一个，不指定时使用第一个基础方案）；Apple 促销优惠的 `signature`（由应用的服务器生成）；Play 的 `replace`（这次购买要替换的订阅，以及替换方式）和 `personalized`。它可能抛出 `notAllowed`（付款被关闭）、`unknownProduct`、`alreadyOwned`。

## 交易

`purchase.events()` 送来商店在购买之外告诉应用的一切：已付款的待处理购买、在应用外完成的购买（在另一台设备上购买、在商店里兑换优惠码）、订阅续期、退款（`.revoked`）、之前没有完成的交易，以及店面的变化。从应用启动起、在有人读取之前就到达的内容，会保留给第一个读取者，所以请在应用启动时读取它。

先交付交易售出的内容，再调用 `finish()`。在完成之前，商店会（在每次启动时）再次送来这笔交易：StoreKit 会一直把它当作未完成，Play 则会给三天内没有完成的购买退款。完成一笔消耗型商品的交易会消耗掉它，用户才能再次购买；`.pending` 的交易不会被完成。

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

`Transaction` 有它的 `id`、`originalId`（同一订阅的各次续期共用它）、`productId`、`kind`、`state`、`quantity`、`date` 和 `expires`（自 1970 年起的纳秒数）、`revoked` 和 `revocationReason`、`autoRenews`、`familyShared`、购买时的 `accountToken`、Apple 的 `environment`、购买时所在的店面 `storefront`、它的 `offerId` 和 `offerType`，以及它的 `verification`。

## 已购权益与恢复购买

`purchase.owned()` 是用户当前拥有的东西（非消耗型商品、处于订阅周期内的订阅、非续期订阅），并且会被追踪：在绑定里读取它时，第一次读取会去查询，之后只要还有地方在读，每次购买、完成、恢复以及每笔交易之后都会重新查询。`owned.has(productId)` 判断其中是否有某个商品。

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

`purchase.restore()` 重新向商店查询用户的购买（在 iOS 上是 `AppStore.sync`，会要求用户登录，所以只能从按钮调用），并返回用户拥有的东西。`purchase.entitlements()` 返回同样的内容，但不会去询问商店。

## 订阅

`purchase.statuses(group)` 返回一个群组里各个订阅的状态（Apple 的订阅群组；在 Play 上是订阅的产品）：它的 `state`（`.subscribed`、`.expired`、`.billingRetry`、`.gracePeriod`、`.revoked`），是否会续期（`willRenew`）以及续期成什么（`renewsTo`），它的 `renewal`、`graceExpires`、`expirationReason`，以及 Apple 签名的 `renewalJWS`。`purchase.manageSubscriptions(productId)` 打开商店里用户的订阅页面（iOS 上是系统面板，Play 上是订阅中心，或者 App Store、Microsoft Store 的账户页面）。`purchase.redeemCode()` 打开优惠码兑换面板（Apple 的）或 Play 的兑换页面；兑换到的东西通过 `events()` 送达。`purchase.requestRefund(t)` 打开 Apple 的退款面板（只有 Apple 有）。`purchase.storefront()` 是商店所在的国家或地区。

## 在服务器上验证

服务器在授予所售内容之前，要先向商店核实这笔购买。交易的 `verification` 里有各商店 API 需要的内容：

- Apple：`jws`，由 App Store 签名的交易。用 App Store Server Library（它的 `SignedDataVerifier`）验证。`verified` 表示 StoreKit 是否已经在设备上验证过它。
- Google Play：`token`，即购买令牌，连同 `packageName`，交给 Google Play Developer API（`purchases.productsv2`、`purchases.subscriptionsv2`）。`originalJSON` 和 `signature` 是用应用的许可密钥签名的购买信息。
- Windows：`token`，即附加产品的 Store ID，交给 Microsoft Store 的收藏 API。

## 测试购买

商店只向它认识的应用售卖商品。在 iOS 上，`asuka dev` 的开发 App 会构建成应用本身，带着应用的 bundle ID，所以商店认识它，保存后也会像平常一样重新加载。在 Android 上，Play 只向从它的某个测试轨道安装的构建售卖商品。

**iOS 模拟器。** 在 Xcode 里创建一个 StoreKit 配置文件（File › New › File › StoreKit Configuration File，也可以从 App Store Connect 同步），填好商品，然后在 `asuka.toml` 里指定它：

```toml
[ios]
storekit = "Products.storekit"
```

`asuka dev` 为模拟器生成的构建会带上这个文件；由 `asuka dev` 启动时，它会像在 Xcode 的 scheme 里设置了这个文件的应用那样，从文件里售卖商品：价格、“询问购买”、交易失败和加速续期都由这个文件控制。其他构建从 App Store 的沙盒购买：用沙盒账户登录，或者安装 TestFlight 构建。

**Android。** 把一个构建上传到内部测试轨道（`asuka publish --platform android --track internal`），在 Play 管理中心添加商品，把你的 Google 账号加入许可测试人员，然后通过测试轨道的链接安装这个构建。许可测试人员不会被真正扣款。

**Windows。** 在合作伙伴中心把应用与商店关联，并从商店安装（可以用软件包外部测试版来测试）。

**在测试里。** `purchase.fake(products)` 给这个包换上一个 `FakeStore`，它售卖 `FakeProduct`，并且像 StoreKit 那样保存交易：在应用完成之前一直是未完成状态，在那之前还会被再次送达（`relaunch()`）。

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
  let _ = store.approve("coins_100")   // 家长批准：events() 送来这笔交易
}
```

`outcome` 可以是 `.bought`、`.pending`、`.cancelled` 或 `.failed`；`unavailable` 和 `notAllowed` 分别模拟没有商店和关闭了付款；`approve(id)` 和 `decline(id)` 是家长对“询问购买”的答复，`deliver(id)` 模拟在另一台设备上的购买，`renew(id)` 一次续期，`expire(id)` 一个到期的订阅，`refund(id)` 一次退款，`setCountry(c)` 一次店面变化；`unfinished()` 列出应用还没有完成的交易，`transactions` 是所有交易，`restores`、`manages`、`redeems` 和 `refunds` 记录了应用发起的调用。

## 配置应用

- **iOS 和 macOS。** 每个 App ID 都默认开启了 App 内购买项目能力，它不需要任何 entitlement。在 App Store Connect 里同意付费 App 协议，并在那里添加商品。Mac 上的 StoreKit 2 需要 macOS 12：更早的版本上 `available()` 为 false。
- **Android。** 导入了 `asuka/purchase` 的应用，构建时会带上 Play Billing 和 `com.android.vending.BILLING` 权限；没有导入的应用不会。
- **Windows。** 在合作伙伴中心为已打包（MSIX）并与商店关联的应用添加附加产品。
- **Web。** 把网站打包成启用了 Play Billing 的 Trusted Web Activity（Bubblewrap 的 `playBilling`），并在 Play 管理中心添加商品。请在三天内用 Play Developer API 从你的服务器确认非消耗型商品。
