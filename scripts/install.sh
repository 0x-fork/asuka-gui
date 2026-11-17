#!/bin/sh
# Installs the asuka CLI from its GitHub release:
#
#	curl -fsSL https://raw.githubusercontent.com/egoist/asuka/main/scripts/install.sh | sh
#
# ASUKA_VERSION picks a release (default: the latest); ASUKA_INSTALL the
# directory (default: ~/.asuka/bin).
set -eu

repo="egoist/asuka"
dir="${ASUKA_INSTALL:-$HOME/.asuka/bin}"

case "$(uname -s)" in
Darwin) os=darwin ;;
Linux) os=linux ;;
*) echo "asuka: this script installs on macOS and Linux; on Windows: npm i -g @egoist/asuka" >&2; exit 1 ;;
esac
case "$(uname -m)" in
arm64 | aarch64) cpu=arm64 ;;
x86_64 | amd64) cpu=x64 ;;
*) echo "asuka: no build for $(uname -m)" >&2; exit 1 ;;
esac
if [ "$os" = darwin ] && [ "$cpu" = x64 ]; then
	echo "asuka: Asuka develops on Macs with Apple silicon, not Intel Macs (the apps it builds run on both: asuka build --arch amd64)" >&2
	exit 1
fi

if [ -n "${ASUKA_VERSION:-}" ]; then
	base="https://github.com/$repo/releases/download/v${ASUKA_VERSION#v}"
else
	base="https://github.com/$repo/releases/latest/download"
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "asuka: downloading asuka-$os-$cpu"
curl -fsSL -o "$tmp/asuka" "$base/asuka-$os-$cpu"
curl -fsSL -o "$tmp/SHA256SUMS" "$base/SHA256SUMS"
want=$(grep " asuka-$os-$cpu\$" "$tmp/SHA256SUMS" | cut -d' ' -f1)
if command -v sha256sum >/dev/null; then got=$(sha256sum "$tmp/asuka" | cut -d' ' -f1); else got=$(shasum -a 256 "$tmp/asuka" | cut -d' ' -f1); fi
if [ "$want" != "$got" ]; then
	echo "asuka: the download doesn't match its checksum" >&2
	exit 1
fi
mkdir -p "$dir"
install -m 0755 "$tmp/asuka" "$dir/asuka"
echo "asuka: installed $dir/asuka ($("$dir/asuka" version))"
case ":$PATH:" in
*":$dir:"*) ;;
*) echo "asuka: add $dir to your PATH" ;;
esac
