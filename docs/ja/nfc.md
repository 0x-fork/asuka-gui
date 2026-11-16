# NFC タグ

`asuka/nfc` は NFC タグを読み書きします。react-native-nfc-manager や Web NFC と同じように、タグの NDEF メッセージ、タグそのもの、タグへの生のコマンドを扱えます。iOS ではシステムのシートを伴う Core NFC、Android では前面にあるアプリの画面に対するリーダーモード、Android の Chrome では Web NFC を使います。Mac、PC、Linux には NFC がありません。

```asuka
import asuka/nfc

component ReadTag {
  state link = ""

  Column {
    Button("Read a Tag") {
      on press {
        do {
          let s = try await nfc.start(alert: "Hold your iPhone near the tag.")
          defer { s.stop() }
          let t = try await s.next()
          link = t.message?.uri() ?? "No link on this tag"
        } catch e {
          link = e.message
        }
      }
    }
    Text(link)
  }
}
```

## NFC の有無とオン・オフ

`nfc.state()` は追跡され、`supported`（デバイスがタグを読める）と `enabled`（NFC がオンになっている。Android の設定のことで、iPhone では常にオン）を持ちます。バインディングの中で読むと、ユーザーが NFC をオンにしたりオフにしたりするのに追従します。`nfc.openSettings()` は Android の NFC の設定を開きます。ほかのプラットフォームでは `NfcError.notAvailable` を投げます。

```asuka
component NfcState {
  let st = nfc.state()

  Column {
    Text(if !st.supported { "This device can't read NFC tags." } else if !st.enabled { "NFC is off." } else { "NFC is on." })
    if st.supported && !st.enabled {
      Button("Turn NFC On") { on press { try? await nfc.openSettings() } }
    }
  }
}
```

## タグの読み取り：セッション

`nfc.start(…)` はタグを探すセッションを始めて、そのセッションを返します。`session.next()` はタグを待ちます。セッションは、止められる（`stop()`）か、失敗する（`fail(message)`。iOS ではシートにそのメッセージが表示されます）まで探し続けます。iOS では、ユーザーがシートを閉じたとき（`next()` が `NfcError.canceled` を投げます）や、60 秒が過ぎたとき（`timeout`）にも終わります。別のセッションを始めると、先に始めたセッションは止まります。

- `alert` は、タグを探しているあいだ iOS のシートに表示される文です。`setAlert` で変更できます（Android と Web はシートを表示しないので、アプリが自分で表示します）。
- `poll` は何を探すかです。`[]` なら NDEF メッセージを持つタグを探します（iOS の NFCNDEFReaderSession。見つかったタグには ID がなく、コマンドも受け付けません）。`[.a, .b, .f, .v]` なら、それらの種類のタグを探します。見つかったタグには ID があり、コマンドも送れます（iOS の NFCTagReaderSession。ISO 7816 のカードには asuka.toml の `nfc.iso7816` が、FeliCa には `nfc.felica` が必要です）。Web は NDEF しか読めません。
- `skipNDEF` は、タグを見つけたときにメッセージを読みません（Android。コマンドを送るだけのタグで速くなります）。`noSound` は音を鳴らしません（Android）。`presenceDelay` は、タグがまだそこにあるかを Android が確認する間隔です。

セッションを始められなかった場合は、`notAvailable`、`disabled`（Android の NFC がオフ）、`denied`（Web の権限）を投げます。

### 一度に一つのタグ

セッションはアプリにタグを一つずつ渡します。次の `next()` は別のタグを探します。アプリがタグを持っているあいだは、そのタグを読み書きできます。

## NDEF メッセージとレコード

タグの `message` は、タグを見つけたときに読んだ NDEF メッセージです（NDEF がないか `skipNDEF` のときは `none`）。その `records` は、それぞれ次のいずれかの `Record` です。

- `.uri(u)`：URI（Well-known タイプ "U"。プレフィックスは省略形で保存されます）
- `.text(t, language:)`：テキスト
- `.mime(type:, data:)`：MIME タイプのデータ
- `.external(type:, data:)`：外部タイプ（`domain:name`）のデータ
- `.androidApp(package)`：Android Application Record（Android はそのパッケージのアプリを開きます）
- `.raw(tnf:, type:, id:, payload:)`：それ以外のレコード。そのままの形で扱います

`message.uri()` は最初のレコードの URI、`message.text()` は最初のテキストです。`message.encode()` は、タグが保持しているのと同じ形でメッセージをバイト列にします。`nfc.decode(bytes)` はそのバイト列を読みます（`NfcError.badMessage` を投げます）。

## タグの書き込みと読み取り専用化

`tag.write(message)` はメッセージを書き込みます（フォーマット可能なタグはフォーマットします）。書き込めるかどうかは `tag.ndef` で分かります（`writable()`、`format`：`.notNDEF`、`.formatable`、`.readWrite`、`.readOnly`、`capacity`：バイト数で、分からなければ -1）。`write` は `readOnly`、`tooLarge`、`tagLost` を投げます。`tag.makeReadOnly()` はタグを永久にロックし、`tag.read()` はメッセージを読み直します。

```asuka
async fn writeLink(u: string) throws {
  let s = try await nfc.start(alert: "Hold your iPhone near a tag to write it.")
  defer { s.stop() }
  let t = try await s.next()
  if !t.ndef.writable() {
    s.fail("This tag can't be written.")
    return
  }
  try await t.write(nfc.Message(records: [nfc.Record.uri(u)]))
  s.setAlert("Written.")
}
```

## タグそのもの：ID、技術、コマンド

タグの `id` はその識別子です（UID。表示するには `id.hex(" ")`。iOS の NDEF セッションでは ID はありません）。`techs` はタグが備える技術で、Android での名前（`nfcA`、`isoDep`、`mifareUltralight`、`felica`、`iso15693`、`ndef` など）で表します。`tag.transceive(data)` は生のコマンドを送って応答を返します。送れるのは、ISO-DEP カードへの ISO 7816 APDU、MIFARE や NFC-A のコマンド、FeliCa のコマンド（長さのバイトは付けません）、ISO 15693 のリクエスト（先頭にフラグとコマンドコード）です。ポーリングするセッションが必要です。

## アプリを開くタグ

アプリが開いていないときでも、タグからアプリを開けます。

- **iOS** はバックグラウンドでタグを読み、最初のレコードがアプリのユニバーサルリンク（asuka.toml の `links`）のいずれかであるタグについて、そのアプリを開くよう提案します。リンクは、ほかのリンクと同じようにアプリの `openURL` に届きます。
- **Android** は、asuka.toml の `nfc.ndef` と `nfc.tech` に当てはまるタグでアプリを開きます。URI のタグは `openURL` に届き、それ以外のタグは `nfc.opened()` に届きます。これは `Tag` のストリームで、タグが近くにあるあいだは、アプリはまだそのタグとやりとりできます。アプリを起動したタグは、最初の読み手が現れるまで待ちます。

```asuka
async fn followOpened() throws {
  for await t in nfc.opened() {
    print("opened by {t.techs.joined(separator: ", ")}")
  }
}
```

## asuka.toml と権限

`asuka/nfc` をインポートしたアプリには、Android の `NFC` 権限と、iOS の `NFCReaderUsageDescription`（独自の説明文は `ios.usage` に書きます）、Core NFC のエンタイトルメント `com.apple.developer.nfc.readersession.formats`（`NDEF` と `TAG`）が付きます。リリースの App ID には NFC Tag Reading のケイパビリティが必要です。

asuka.toml の `nfc` は、アプリのタグに必要なものを加えます。

```toml
[nfc]
iso7816 = ["D2760000850101"]
felica = ["8008"]
ndef = [
  {uri = "https://example.com/t/"},
  {mime = "application/vnd.example.ticket"},
  {external = "example.com:ticket"},
]
tech = [["IsoDep"], ["NfcA", "MifareUltralight"]]
required = true
```

| キー | 説明 |
| --- | --- |
| `iso7816` | iOS がカードを見つけたときに選択する ISO 7816 のアプリケーション（AID、16 進数） |
| `felica` | iOS がポーリングする FeliCa のシステムコード |
| `ndef` | Android でアプリを開くタグ。最初のレコードで指定します：`uri`、`mime`、`external` |
| `tech` | Android で、技術によってアプリを開くタグ |
| `required` | NFC のあるスマートフォン専用のアプリにします。Google Play は、ほかの端末にはアプリを提供しません |

## プラットフォームごとの動作

| プラットフォーム | 動作 |
| --- | --- |
| iOS | Core NFC：`poll` がなければ NFCNDEFReaderSession、あれば NFCTagReaderSession。システムのシートとそのアラート文。ISO 7816、MIFARE、FeliCa、ISO 15693 のコマンド。ユニバーサルリンクによる、バックグラウンドでのタグの読み取り。NFC のオン・オフの切り替えはありません |
| Android | 前面のアクティビティに対するリーダーモード。NDEF、NdefFormatable、ISO-DEP、NFC-A/B/F/V、MIFARE Ultralight。アプリを開くタグ。`openSettings` |
| Web | Web NFC の `NDEFReader`（Android の Chrome、https）：NDEF の読み書き、`makeReadOnly`、タグのシリアル番号。生のコマンド、容量、ページを開くタグはありません。ほかのブラウザーでは `notAvailable` |
| macOS、Windows、Linux | アプリから使える NFC はありません。`state()` は非対応と答え、`start` は `notAvailable` を投げます |

## 模擬タグを使ったテスト

`nfc.fake()` は `FakeNfc` をパッケージに与えます。セッションがタグを探しているあいだ（`session` が none でないあいだ。その `alert` も分かります）、`tap(tag)` は `FakeTag` をセッションに渡します。`FakeTag` には、`id`、`ndef` のバイト列、`readOnly`、`formatable`、`capacity`、`techs`、生のコマンドへの応答をコマンドの 16 進数ごとに定めた `answers` があります。`cancel()` はユーザーが iOS のシートを閉じる操作、`open(tag)` はシステムがアプリを開くときのタグ、`setEnabled(on)` は NFC のオン・オフの切り替えです。`tags` はタップしたタグをアプリが操作した後の状態で表し（`writes`、その `ndef`）、`sessions` と `settings` は行われた呼び出しを表します。

```asuka
test "a link, read" {
  let n = nfc.fake()
  n.setEnabled(true)
  expect(n.tags.isEmpty())
  let m = nfc.Message(records: [nfc.Record.uri("https://example.com/hello")])
  expect((try nfc.decode(m.encode())).uri() == "https://example.com/hello")
}
```
