#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# Checks an install of mirrortest (plan 9.8), the tree `cmake --install` or the unpacked package leaves:
#
#   tools/check_install.sh PREFIX [BUILD_DIR...]
#
# The installed program runs and says its version, the licence and the documentation are there, the links between the pages
# of the documentation lead to pages that are there, and no file of the install holds the path of a build directory or of the
# source tree (BUILD_DIR..., and the repository this script is in). Exits 0 if all is as it should be.

set -u

prefix=${1:-}
if [ -z "$prefix" ] || [ ! -d "$prefix" ]; then
    echo "usage: tools/check_install.sh PREFIX [BUILD_DIR...]" >&2
    exit 2
fi
shift
here=$(cd "$(dirname "$0")/.." && pwd)
prefix=$(cd "$prefix" && pwd)
fail=0
bad() { echo "check_install: $*" >&2; fail=1; }

version=$("$prefix/bin/mirrortest" --version 2>&1) || bad "bin/mirrortest does not run: $version"
case "$version" in mirrortest\ *) echo "$version" ;; *) bad "bin/mirrortest --version says '$version'" ;; esac

doc=$prefix/share/doc/mirrortest
for f in LICENSE README.md docs/conventions.md docs/design.md docs/templates.md; do
    [ -f "$doc/$f" ] || bad "share/doc/mirrortest/$f is not installed"
done

# Links of the form ](page.md) or ](page.md#anchor) to another page, relative to the page that holds them. (Links to source
# files, which the design refers to, are not pages, and code such as `[](int)` is not a link.)
while IFS= read -r page; do
    dir=$(dirname "$page")
    while IFS= read -r link; do
        [ -z "$link" ] && continue
        case "$link" in http://*|https://*|mailto:*|\#*) continue ;; esac
        target=${link%%#*}
        case "$target" in *.md) ;; *) continue ;; esac
        [ -e "$dir/$target" ] || bad "${page#$prefix/}: the link '$link' leads nowhere in the install"
    done < <(grep -o ']([^)]*)' "$page" | sed 's/^](//; s/)$//')
done < <(find "$doc" -name '*.md' | sort)

for dir in "$here" "$@"; do
    [ -d "$dir" ] || continue
    dir=$(cd "$dir" && pwd)
    if grep -rlaF -- "$dir" "$prefix" > /dev/null 2>&1; then
        bad "a file of the install holds the path $dir: $(grep -rlaF -- "$dir" "$prefix" | head -3 | tr '\n' ' ')"
    fi
done

[ $fail -eq 0 ] && echo "check_install: ok"
exit $fail
