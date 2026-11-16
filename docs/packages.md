# Packages

Packages share Asuka code between apps: components, stores, native
packages (with their Swift, Kotlin and JavaScript), and plugins (with
their prebuilt libraries). They're published on the **npm registry**,
but npm doesn't install them. The `asuka` CLI does, and it needs no
Node.

## Using a package

```sh
asuka add @acme/charts            # the latest version
asuka add @acme/charts@^1.2       # a range
asuka remove @acme/charts
```

`asuka add` writes the dependency in `asuka.toml`, resolves the
versions, and records them in `asuka.lock`:

```toml
[dependencies]
"@acme/charts" = "^1.2.0"
```

Then import it by its name:

```asuka
import @acme/charts

charts.LineChart(points) { height: 200 }
```

`asuka dev`, `asuka build` and `asuka test` fetch what `asuka.lock`
names, the first time, and use it from then on. Commit `asuka.lock`, so
everyone builds with the same versions. `asuka install` fetches
everything ahead of time (CI); `asuka install -update` resolves again
from `asuka.toml`'s ranges.

### Where packages come from

| In asuka.toml | What it is |
|---|---|
| `"@acme/charts" = "^1.2.0"` | a range of versions on the registry |
| `charts2 = "npm:@acme/charts@^2.0.0"` | another package, imported as `charts2`: a second major version |
| `charts = "github:acme/charts#v1.2.0"` | a tag, branch or commit of a GitHub repository |
| `charts = "file:../charts"` | a directory beside the project, while you work on both |

Private packages come from the registries `.npmrc` names, with the
tokens it has, as npm reads them: a company's registry for a scope
(`@acme:registry=https://npm.acme.dev/`), GitHub Packages, Verdaccio.
`NPM_TOKEN` is the token for the default registry.

### Which versions

Each package has **one version** in an app: the least version that
meets every range that asks for it (minimal version selection, as Go
modules do it). A build never changes because someone published
something, and two people with the same `asuka.lock` build the same
thing.

Two major versions of a package are two packages. When one dependency
needs `@acme/charts` 1.x and you need 2.x, depend on 2.x under another
name (`charts2 = "npm:@acme/charts@^2.0.0"`) and import it by that
name. `asuka add` says so when it happens.

### What's checked

Every tarball is checked against the hash `asuka.lock` records
(SHA-512, the registry's integrity) before it's used, and kept in a
cache shared by every project (`~/.cache/asuka/pkg` on Linux; the
system's cache directory elsewhere), by that hash. Nothing a package
contains runs when it's installed: there are no install scripts.

## Making a package

A package is a library: a directory with an `asuka.toml` and its
`.asuka` files. `asuka new --lib ./charts` makes one, with a function and
its test block (`asuka new` alone makes an app):

```toml
name = "@acme/charts"
version = "1.2.0"
description = "Charts drawn with Asuka's Canvas"
license = "MIT"
repository = "github:acme/charts"

[dependencies]
"@acme/colors" = "^0.3.0"
```

Its `pub` declarations are what apps import. A package can hold native
code too ([Native code](native.md)) and prebuilt libraries
([Plugins](plugins.md)); they're published with it.

### Publishing

```sh
asuka pkg publish                 # with your npm login, or NPM_TOKEN
asuka pkg publish -otp 123456     # an account with two-factor authentication
asuka pkg publish -tag next       # a pre-release
```

`asuka pkg publish` packs the directory (not `.git`, `node_modules`,
`dist` or `asuka.lock`) the same way every time, so the same sources
make the same tarball, writes the `package.json` the registry reads,
and uploads it. Log in once with `npm login`, or set `NPM_TOKEN`. On CI,
publishing with npm (`npm publish --provenance` in the directory
`asuka pkg pack` writes) adds a provenance statement.

Versions are semver. Before it uploads, `asuka pkg publish` compares
the package's `pub` API with the last version's: removing a declaration
or changing it in a way that breaks its users needs a new major
version.

## The standard library and the platform SDK

`std/http`, `std/json`, `std/storage` and the rest of the standard
library, and `platform/camera`, `platform/notify` and the rest of the
platform SDK, come with the CLI, versioned with it: they need no `asuka
add`. A package can't be named under `std/` or `platform/` (`asuka pkg
publish` and `asuka add` refuse it).
