# アプリ内課金

`platform/purchase` を使うと、各プラットフォームのストアを通じてアプリ内で商品を販売できます。扱えるのは、消耗型（コイン、ヒント）、非消耗型（レベル、買い切りの「Pro」）、自動更新されるサブスクリプションと、自動更新されないサブスクリプションです。react-native-iap、expo-iap、RevenueCat の SDK がデバイス上で行うことをカバーし、各ストアの現行の API を使います。

| プラットフォーム | ストア | API |
| --- | --- | --- |
| iOS、macOS | App Store | StoreKit 2 |
| Android | Google Play | Play Billing Library 9 |
| Windows | Microsoft Store | `Windows.Services.Store`（Store からインストールしたアプリの場合） |
| Web | Google Play | Trusted Web Activity の中での Digital Goods API と Payment Request |
| Linux | なし | |

ストアがない環境（Linux、Digital Goods API のないブラウザー、Store からインストールしていない Windows アプリ）では、`purchase.available()` は false になり、それ以外の呼び出しは `PurchaseError.notAvailable` を投げます。

## 商品

`Item` はアプリが販売する商品で、ストアでの ID と種類（`.consumable`、`.nonConsumable`、`.autoRenewable`、`.nonRenewing`）からなります。Play は消耗型と非消耗型を区別しないので、アプリが指定します。

```asuka
import platform/purchase

let coins = purchase.Item(id: "coins_100", kind: .consumable)
let pro = purchase.Item(id: "pro", kind: .nonConsumable)
let plus = purchase.Item(id: "plus_monthly", kind: .autoRenewable)
```

`purchase.declare(items)` は、ストアに問い合わせずに、アプリが何を販売しているかを伝えます。アプリの起動時に呼んでください。そうしておくと、先に届いた消耗型のトランザクション（前回完了しなかったもの）が、完了したときに消費されます。`purchase.products(items)` はストアにそれらの商品を問い合わせます。結果はユーザーの国と言語のもので、ストアが販売していない商品は含まれません。`Product` には、`title`、`description`、`price`（表示用。「$4.99」など）、`priceMicros` と `currency`、`item()`（購入に使う `Item`）があり、サブスクリプションなら `subscription` もあります。`subscription` には、`group`、`level`、`period`（`unit`、`count`）、`offers`（それぞれが `phases` のリストで、無料トライアルや一定期間の割引価格などからなります）、ユーザーが導入オファーを受けられるかどうか（`introEligible`）があります。

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

## 購入

`purchase.buy(item, options)` はストアのシートを表示し、ユーザーが購入したとき、キャンセルしたとき、または承認待ちになったときに結果を返します。その `Result` の `status` は、`.bought`（`transaction` 付き）、`.pendingApproval`（「承認と購入のリクエスト」（Ask to Buy）や、Play が待っている支払い。トランザクションは後から `events()` で届きます）、`.cancelled` のいずれかです。

```asuka
async fn buyCoins() throws {
  let r = try await purchase.buy(coins)
  if r.status == .bought {
    if let t = r.transaction { await deliver(t) }
  }
}
```

`Options` には、消耗型の `quantity`、購入をアプリのユーザーに結び付ける `accountToken`（UUID。Apple では UUID でなければなりません）、Play の `profileToken`、サブスクリプションを購入するときの `offer`（Play では必須で、指定しなければ最初の基本プランになります）、Apple のプロモーションオファーの `signature`（アプリのサーバーから取得します）、Play の `replace`（このサブスクリプションが置き換えるサブスクリプションと、その置き換え方）と `personalized` があります。`buy` は `notAllowed`（支払いがオフになっている）、`unknownProduct`、`alreadyOwned` を投げます。

## トランザクション

`purchase.events()` は、購入の操作以外でストアが知らせてくることを届けます。保留中だった購入の支払い完了、アプリの外での購入（別のデバイスでの購入や、ストアで引き換えたオファーコード）、サブスクリプションの更新、返金（`.revoked`）、以前から未完了のままのトランザクション、ストアフロントの変更です。アプリの起動後、まだ誰も読んでいないうちに届いたものは、最初の読み手のために保持されます。アプリの起動時に読んでください。

トランザクションの内容を提供してから、`finish()` を呼びます。それまでは、ストアは同じトランザクションを（起動のたびに）また届けます。StoreKit は未完了のまま保持し、Play は 3 日以内に完了しなかった購入を返金します。消耗型を完了するとその商品は消費され、再び購入できるようになります。`.pending` のトランザクションは完了しません。

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

`Transaction` には、`id`、`originalId`（サブスクリプションの更新はこれを共有します）、`productId`、`kind`、`state`、`quantity`、`date`、`expires`、`revoked`（どれも `time.Time?` で、ストアが知らせなければ none）、`revocationReason`、`autoRenews`、`familyShared`、購入時の `accountToken`、Apple の `environment`、購入したストアフロント（`storefront`）、`offerId` と `offerType`、そして `verification` があります。

## 所有しているものと購入の復元

`purchase.owned()` は、ユーザーがいま所有しているもの（非消耗型、期間内のサブスクリプション、自動更新されないサブスクリプション）で、追跡されます。バインディングの中で読むと、最初に読まれたときにストアに問い合わせ、読まれているあいだは、購入、完了、復元、トランザクションのたびに問い合わせ直します。`owned.has(productId)` は、所有しているものにその商品が含まれるかどうかを返します。

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

`purchase.restore()` は、ユーザーの購入をストアに問い合わせ直し（iOS では `AppStore.sync`。ユーザーにサインインを求めるので、ボタンから呼ぶときだけ使います）、所有しているものを返します。`purchase.entitlements()` は、問い合わせ直さずにそれを返します。

## サブスクリプション

`purchase.statuses(group)` は、グループに属するサブスクリプションの状態です（グループは Apple ではサブスクリプショングループ、Play ではサブスクリプションの商品）。`state`（`.subscribed`、`.expired`、`.billingRetry`、`.gracePeriod`、`.revoked`）、更新されるかどうか（`willRenew`）と何として更新されるか（`renewsTo`）、`renewal` と `graceExpires`（どちらも `time.Time?`）、`expirationReason`、Apple が署名した `renewalJWS` があります。`purchase.manageSubscriptions(productId)` は、ユーザーのサブスクリプションを管理するストアのページを表示します（iOS ではシート、Play では定期購入センター、App Store や Microsoft Store ではアカウントのページ）。`purchase.redeemCode()` は、オファーコードのシート（Apple）か Play の引き換えページを表示します。それで購入したものは `events()` で届きます。`purchase.requestRefund(t)` は Apple の返金リクエストのシートを表示します（Apple だけ）。`purchase.storefront()` はストアの国です。

## サーバーでの検証

サーバーは、販売したものを付与する前に、購入をストアに確認します。トランザクションの `verification` には、各ストアの API が受け取るものが入っています。

- Apple：`jws`。App Store が署名したトランザクションです。App Store Server Library（の `SignedDataVerifier`）で検証します。`verified` は、StoreKit がデバイス上ですでに確認したかどうかを表します。
- Google Play：`token`（購入トークン）と `packageName`。Google Play Developer API（`purchases.productsv2`、`purchases.subscriptionsv2`）に渡します。`originalJSON` と `signature` は、アプリのライセンスキーで署名された購入情報です。
- Windows：`token`（アドオンの Store ID）。Microsoft Store のコレクション API に渡します。

## 購入のテスト

ストアは、自分が知っているアプリにしか販売しません。iOS では、`asuka dev` の開発用アプリはアプリそのものとして、アプリの bundle ID でビルドされるので、ストアはそのアプリを知っています。保存すると、ほかのアプリと同じようにリロードされます。Android では、Play は自分のトラックのいずれかからインストールしたビルドにしか販売しません。

**iOS シミュレーター。** Xcode で StoreKit 構成ファイルを作り（File › New › File › StoreKit Configuration File。App Store Connect から同期することもできます）、商品を追加して、`asuka.toml` で指定します。

```toml
[ios]
storekit = "Products.storekit"
```

`asuka dev` がシミュレーター向けに作るビルドはこのファイルを含み、`asuka dev` が起動したときには、このファイルの商品を販売します。Xcode から実行するアプリで、スキームにこのファイルを設定した場合と同じです。価格、「承認と購入のリクエスト」、失敗するトランザクション、速く進む更新は、このファイルで決まります。それ以外のビルドは App Store のサンドボックスで購入します。サンドボックスアカウントでサインインするか、TestFlight のビルドをインストールしてください。

**Android。** 内部テストトラックにビルドをアップロードし（`asuka publish --platform android --track internal`）、Play Console で商品を追加し、自分の Google アカウントをライセンステスターに加えてから、トラックのリンクからビルドをインストールします。ライセンステスターに料金が請求されることはありません。

**Windows。** パートナーセンターでアプリを Store に関連付け、Store からインストールします（テストにはパッケージフライトを使います）。

**テストでは。** `purchase.fake(products)` は、`FakeProduct` を販売する `FakeStore` をパッケージに与えます。`FakeStore` は StoreKit と同じようにトランザクションを保持します。アプリが完了させるまでは未完了のままで、それまでは（`relaunch()` で）再び届けられます。

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
  let _ = store.approve("coins_100")   // 保護者が承認する：events() に届く
}
```

`outcome` は `.bought`、`.pending`、`.cancelled`、`.failed` のいずれかです。`unavailable` と `notAllowed` は、ストアがない状態と、支払いがオフになっている状態を表します。`approve(id)` と `decline(id)` は「承認と購入のリクエスト」への保護者の回答、`deliver(id)` は別のデバイスでの購入、`renew(id)` は更新、`expire(id)` はサブスクリプションの終了、`refund(id)` は返金、`setCountry(c)` はストアフロントの変更です。`unfinished()` はアプリがまだ完了していないものの一覧、`transactions` はすべてのトランザクション、`restores`、`manages`、`redeems`、`refunds` は行われた呼び出しです。

## アプリの設定

- **iOS と macOS。** App 内課金のケイパビリティはすべての App ID で有効になっていて、エンタイトルメントは必要ありません。App Store Connect で有料 App 契約（Paid Apps Agreement）に同意し、そこで商品を追加します。Mac の StoreKit 2 には macOS 12 が必要で、それより前のバージョンでは `available()` は false です。
- **Android。** `platform/purchase` をインポートするアプリは、Play Billing と `com.android.vending.BILLING` 権限を含めてビルドされます。インポートしないアプリには含まれません。
- **Windows。** パッケージ化（MSIX）され、Store に関連付けられたアプリに、パートナーセンターでアドオンを追加します。
- **Web。** サイトを、Play Billing を有効にした Trusted Web Activity としてパッケージ化し（Bubblewrap の `playBilling`）、Play Console で商品を追加します。非消耗型は、3 日以内にサーバーから Play Developer API で承認（acknowledge）してください。
