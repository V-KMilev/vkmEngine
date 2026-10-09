#!/bin/sh
# Installs vkmEngine for this user, with no root: the newest release (or VKM_VERSION=1.2.3)
# into ~/.local/share/vkmEngine, `vkm` into ~/.local/bin, and the editor into the app menu.
# Its games build with GCC, or with Clang under VKM_COMPILER=clang. Installing again
# replaces the engine; ~/.local/share/vkmEngine/uninstall.sh removes it.
#
#   curl -fsSL https://github.com/V-KMilev/vkmEngine/releases/latest/download/install.sh | sh
set -eu

repo="V-KMilev/vkmEngine"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
dest="$data/vkmEngine"
bindir="$HOME/.local/bin"
desktop="$data/applications/vkmengine.desktop"

say() { printf 'vkmEngine: %s\n' "$*"; }
die() { printf 'vkmEngine: %s\n' "$*" >&2; exit 1; }

case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) platform=linux-x64 ;;
    *) die "there is no build for $(uname -s) $(uname -m); vkmEngine ships for Linux and Windows on x86-64" ;;
esac
compiler="${VKM_COMPILER:-gcc}"
case "$compiler" in
    gcc|clang) ;;
    *) die "VKM_COMPILER is gcc or clang, not '$compiler'" ;;
esac
command -v curl > /dev/null || die "curl is needed to download the engine"
command -v tar > /dev/null || die "tar is needed to unpack the engine"
command -v sha256sum > /dev/null || die "sha256sum is needed to check the engine"

# The newest release is the tag GitHub's /latest redirects to. VKM_DOWNLOADS serves a
# release's files from elsewhere - CI installs the archive it just built - and names no latest.
version="${VKM_VERSION:-}"
if [ -z "$version" ] && [ -n "${VKM_DOWNLOADS:-}" ]; then
    die "VKM_DOWNLOADS needs VKM_VERSION: there is no newest release to ask it for"
fi
if [ -z "$version" ]; then
    latest=$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$repo/releases/latest")
    version="${latest##*/v}"
fi
name="vkmEngine-$version-$platform-$compiler"
archive="$name.tar.xz"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

say "downloading $version, for $compiler"
release="${VKM_DOWNLOADS:-https://github.com/$repo/releases/download/v$version}"
curl -fL --progress-bar -o "$work/$archive" "$release/$archive" || die "no $archive in release v$version"

# Checked before it is unpacked: a download cut short or changed on its way is not installed.
curl -fsSL -o "$work/SHA256SUMS" "$release/SHA256SUMS" \
    || die "release v$version carries no SHA256SUMS to check $archive against"
# A name marked '*' was summed in binary mode, as Windows builds of sha256sum write it.
sum=$(awk -v name="$archive" '$2 == name || $2 == "*" name { print $1 }' "$work/SHA256SUMS")
[ -n "$sum" ] || die "the release's SHA256SUMS does not name $archive"
echo "$sum  $work/$archive" | sha256sum -c --status - \
    || die "$archive does not match the release's SHA256SUMS; nothing was installed"
tar -xJf "$work/$archive" -C "$work"

# Swapped whole, so a failed unpack leaves the old engine as it was.
mkdir -p "$data"
rm -rf "$dest.old"
[ -d "$dest" ] && mv "$dest" "$dest.old"
mv "$work/$name" "$dest"
rm -rf "$dest.old"

mkdir -p "$bindir"
ln -sf "$dest/vkm" "$bindir/vkm"

mkdir -p "$(dirname "$desktop")"
cat > "$desktop" <<EOF
[Desktop Entry]
Type=Application
Name=vkmEngine
Comment=Make games with vkmEngine $version
Exec="$dest/bin/vkm_editor"
Icon=$dest/assets/logo/vkm_engine_icon.png
Terminal=false
Categories=Development;
EOF

# Everything above, and the tools vkm fetched.
cat > "$dest/uninstall.sh" <<EOF
#!/bin/sh
rm -f "$bindir/vkm" "$desktop"
rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/vkm" "$dest"
echo "vkmEngine: removed. Your projects are where you left them."
EOF
chmod +x "$dest/uninstall.sh"

say "installed $version in $dest; \`vkm doctor\` checks what else this machine needs"
case ":$PATH:" in
    *":$bindir:"*) say "run \`vkm new mygame\`, or open vkmEngine from your app menu" ;;
    *) say "add $bindir to your PATH to run \`vkm\` (most distributions do at the next login)" ;;
esac
