# NFC tags

`asuka/nfc` reads and writes NFC tags: their NDEF messages, the tags
themselves, and raw commands to them, as react-native-nfc-manager and Web
NFC do. iOS's Core NFC, with the system's sheet; Android's reader mode
for the app's screen in front; Web NFC in Chrome on Android. A Mac, a PC
and Linux have none.

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

## Is NFC there and on

`nfc.state()` is tracked: its `supported` (the device reads tags) and
`enabled` (NFC is on: Android's setting; an iPhone's is always on). Read
in a binding, it follows the user turning NFC on or off.
`nfc.openSettings()` opens Android's NFC settings; elsewhere it throws
`NfcError.notAvailable`.

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

## Reading tags: sessions

`nfc.start(…)` begins a session that looks for tags, and returns it;
`session.next()` waits for a tag. A session looks until it's stopped
(`stop()`), fails (`fail(message)`: iOS's sheet shows the message), or,
on iOS, the user closes the sheet (`next()` throws `NfcError.canceled`)
or sixty seconds pass (`timeout`). Starting another stops the first.

- `alert` is what iOS's sheet says while it looks; `setAlert` changes it
  (Android and the web show no sheet: the app shows its own).
- `poll` is what it looks for: `[]`, tags with an NDEF message (iOS's
  NFCNDEFReaderSession, whose tags have no ID and take no commands);
  `[.a, .b, .f, .v]`, those kinds of tags, with their IDs and commands
  (iOS's NFCTagReaderSession, which needs asuka.toml's `nfc.iso7816` for
  ISO 7816 cards and `nfc.felica` for FeliCa). The web reads NDEF only.
- `skipNDEF` doesn't read the message as a tag is found (Android:
  faster, for tags that are only sent commands); `noSound` plays no
  sound (Android); `presenceDelay` is how often Android checks the tag is
  still there.

It throws `notAvailable`, `disabled` (Android's NFC off) or `denied`
(the web's permission) if it can't begin.

### One tag at a time

A session hands the app one tag at a time: the next `next()` looks for
another. While the app holds a tag, it reads and writes it.

## NDEF messages and records

A tag's `message` is its NDEF message, read as it was found (`none`: no
NDEF, or `skipNDEF`): its `records`, each a `Record`:

- `.uri(u)`: a URI (well-known type "U", its prefix abbreviated);
- `.text(t, language:)`: a text;
- `.mime(type:, data:)`: a MIME type's data;
- `.external(type:, data:)`: an external type's (`domain:name`);
- `.androidApp(package)`: an Android Application Record (Android opens
  the app of the package);
- `.raw(tnf:, type:, id:, payload:)`: any other, as it is.

`message.uri()` is the first record's URI, `message.text()` the first
text; `message.encode()` the message's bytes, as a tag holds them, and
`nfc.decode(bytes)` reads them (it throws `NfcError.badMessage`).

## Writing and making tags read-only

`tag.write(message)` writes a message (formatting a formatable tag);
`tag.ndef` says whether it can (`writable()`, `format`: `.notNDEF`,
`.formatable`, `.readWrite`, `.readOnly`; `capacity` in bytes, -1 if not
known). It throws `readOnly`, `tooLarge`, `tagLost`.
`tag.makeReadOnly()` locks it for good; `tag.read()` reads the message
again.

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

## The tag itself: IDs, technologies, commands

A tag's `id` is its identifier (UID, `id.hex(" ")` to show it; none in
iOS's NDEF sessions), its `techs` the technologies it has, as Android
names them (`nfcA`, `isoDep`, `mifareUltralight`, `felica`, `iso15693`,
`ndef`, ...). `tag.transceive(data)` sends a raw command and returns the
answer: an ISO 7816 APDU to an ISO-DEP card, a MIFARE or NFC-A command,
a FeliCa command (without its length byte), an ISO 15693 request (its
flags and command code first). It needs a session that polls.

## Tags that open the app

A tag can open the app when it isn't open:

- **iOS** reads tags in the background and offers to open an app for a
  tag whose first record is one of its universal links (asuka.toml's
  `links`). The link comes to the app's `openURL`, as any link does.
- **Android** opens the app for the tags of asuka.toml's `nfc.ndef` and
  `nfc.tech`. A URI's tag comes to `openURL`; the others to
  `nfc.opened()`, a stream of `Tag`s the app can still talk to while
  they're near. The tag the app launched with waits for the first
  reader.

```asuka
async fn followOpened() throws {
  for await t in nfc.opened() {
    print("opened by {t.techs.joined(separator: ", ")}")
  }
}
```

## asuka.toml and permissions

An app that imports `asuka/nfc` gets Android's `NFC` permission, and on
iOS `NFCReaderUsageDescription` (write your own in `ios.usage`) and Core
NFC's entitlement, `com.apple.developer.nfc.readersession.formats`
(`NDEF` and `TAG`). A release's App ID needs the NFC Tag Reading
capability.

asuka.toml's `nfc` adds what the app's tags need:

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

| Key | What it is |
| --- | --- |
| `iso7816` | The ISO 7816 applications (AIDs, hex) iOS selects as it finds a card |
| `felica` | The FeliCa system codes iOS polls for |
| `ndef` | Android's tags that open the app, by their first record: `uri`, `mime`, `external` |
| `tech` | Android's tags that open the app by technologies |
| `required` | The app is for phones with NFC alone: Google Play offers it to no others |

## On each platform

| Platform | What it does |
| --- | --- |
| iOS | Core NFC: NFCNDEFReaderSession without `poll`, NFCTagReaderSession with it; the system's sheet and its alert; ISO 7816, MIFARE, FeliCa and ISO 15693 commands; background tag reading through universal links. No NFC switch |
| Android | Reader mode for the activity in front; NDEF, NdefFormatable, ISO-DEP, NFC-A/B/F/V and MIFARE Ultralight; tags that open the app; `openSettings` |
| The web | Web NFC's `NDEFReader` (Chrome on Android, https): NDEF reading and writing, `makeReadOnly`, the tag's serial number. No raw commands, no capacity, no tags opening the page. Other browsers: `notAvailable` |
| macOS, Windows, Linux | No NFC for apps: `state()` says unsupported, `start` throws `notAvailable` |

## Testing with simulated tags

`nfc.fake()` gives the package a `FakeNfc`: while a session looks
(`session` isn't none, with its `alert`), `tap(tag)` hands it a
`FakeTag` (its `id`, `ndef` bytes, `readOnly`, `formatable`,
`capacity`, `techs`, `answers` to raw commands by their hex); `cancel()`
is the user closing iOS's sheet; `open(tag)` is a tag the system opens
the app with; `setEnabled(on)` turns NFC on or off. `tags` are the tags
tapped as the app left them (`writes`, their `ndef`), `sessions` and
`settings` the calls made.

```asuka
test "a link, read" {
  let n = nfc.fake()
  n.setEnabled(true)
  expect(n.tags.isEmpty())
  let m = nfc.Message(records: [nfc.Record.uri("https://example.com/hello")])
  expect((try nfc.decode(m.encode())).uri() == "https://example.com/hello")
}
```
