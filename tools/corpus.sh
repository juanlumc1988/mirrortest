#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# Runs mirrortest on code that was not written for it, and reports what a unit test would not have found:
# a crash, a hang, a sanitizer report, an `error` that is not the tool saying no on purpose, and the headers
# the two frontends read differently. It is a development tool (design: plan 9.1); the corpus is other
# people's code, so nothing here is part of the build or of CI.
#
#   tools/corpus.sh --bin BUILD/mirrortest --dump BUILD/mt_dump_model --out DIR [options] ROOT...
#
#   --bin PATH       the mirrortest to run (build it with -fsanitize=address,undefined to find memory errors)
#   --dump PATH      mt_dump_model (-DMT_BUILD_TOOLS=ON), to compare the models of the two frontends
#   --out DIR        where the files go (created; DIR/report.txt is the answer)
#   -I DIR           an include directory, for libclang and for the includes of gen-tests (repeatable)
#   --std STD        the C++ standard libclang parses with (default c++17)
#   --jobs N         headers in parallel (default 4)
#   --timeout S      seconds before a run counts as a hang (default 60)
#   --ext "h hpp"    extensions of the files to take from the ROOTs (default "h hpp hxx hh"); "" takes every file
#   --config FILE    a configuration for both commands and the dumper (`ignore_macros` is what matters: a project's own
#                    macros, which a syntax tree cannot see through and a compiler can)
#   --frontends "ts cl"   which frontends run gen-tests (default both; "ts" for a build without libclang)
#   --tests          ROOTs hold test files: run gen-impl on them (tree-sitter only) instead of gen-tests on headers
#
# ROOT is a directory (every file with a listed extension below it, in sorted order) or a file.
#
# Files in DIR: summary.tsv (one line per file and frontend), report.txt, and for every file the
# diagnostics (*.err), the output (*.out) and, for headers, the two models (*.ts.model, *.cl.model).

set -u

bin=""; dump=""; out=""; std="c++17"; jobs=4; limit=60; exts="h hpp hxx hh"; tests=0; frontends="ts cl"; config=""
includes=(); roots=()
while [ $# -gt 0 ]; do
    case "$1" in
        --bin) bin=$2; shift 2 ;;
        --dump) dump=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        -I) includes+=("$2"); shift 2 ;;
        --std) std=$2; shift 2 ;;
        --jobs) jobs=$2; shift 2 ;;
        --timeout) limit=$2; shift 2 ;;
        --ext) exts=$2; shift 2 ;;
        --frontends) frontends=$2; shift 2 ;;
        --config) config=$(realpath "$2"); shift 2 ;;
        --tests) tests=1; shift ;;
        -h|--help) sed -n '5,32p' "$0"; exit 0 ;;
        -*) echo "corpus.sh: unknown option $1" >&2; exit 2 ;;
        *) roots+=("$1"); shift ;;
    esac
done
if [ -z "$bin" ] || [ -z "$out" ] || [ ${#roots[@]} -eq 0 ]; then
    echo "usage: tools/corpus.sh --bin PATH --out DIR [--dump PATH] [-I DIR]... ROOT..." >&2
    exit 2
fi
if [ "$tests" = 0 ] && [ -z "$dump" ]; then
    echo "corpus.sh: warning: no --dump, so the two frontends are not compared" >&2
fi
[ -x "$bin" ] || { echo "corpus.sh: no such program: $bin" >&2; exit 2; }
bin=$(realpath "$bin")
if [ -n "$dump" ]; then
    [ -x "$dump" ] || { echo "corpus.sh: no such program: $dump" >&2; exit 2; }
    dump=$(realpath "$dump")
fi
mkdir -p "$out"; out=$(realpath "$out")

# The files, in an order that does not depend on the file system.
list="$out/files.txt"
: > "$list"
for root in "${roots[@]}"; do
    if [ -f "$root" ]; then realpath "$root" >> "$list"; continue; fi
    if [ -z "$exts" ]; then
        find "$root" -type f | sort | xargs -r realpath >> "$list"
    else
        for e in $exts; do find "$root" -type f -name "*.$e"; done | sort -u | xargs -r realpath >> "$list"
    fi
done
sort -u -o "$list" "$list"

inc=""
for d in "${includes[@]+"${includes[@]}"}"; do
    [ -d "$d" ] || { echo "corpus.sh: no such include directory: $d" >&2; exit 2; }
    inc="$inc -I $(realpath "$d")"
done

# One file: run it, write one summary line. Exit codes of mirrortest: 0 ok, 1 errors in the input, 3 I/O;
# anything else (usage 2, a signal 128+n, 124 from timeout) is what this is looking for.
one() {
    file=$1
    read -r -a inc_args <<< "$inc"   # paths with spaces are not supported, which no dependency tree has
    cfg=(); dcfg=(); if [ -n "$config" ]; then cfg=(--config "$config"); dcfg=(--config "$config"); fi
    id=$(echo "$file" | sed 's|^/||; s|[/ ]|_|g')
    run() {   # label, command...
        label=$1; shift
        start=$(date +%s.%N)
        timeout "$limit" "$@" > "$out/$id.$label.out" 2> "$out/$id.$label.err"
        rc=$?
        end=$(date +%s.%N)
        errors=$(grep -c ': error:' "$out/$id.$label.err")
        warnings=$(grep -c ': warning:' "$out/$id.$label.err")
        # UBSan's check of function types fires in tree-sitter's generated C (the scanner is declared without
        # a prototype), which is not ours to change: it is left out.
        sanitizer=$(grep -E 'ERROR: AddressSanitizer|runtime error:|LeakSanitizer' "$out/$id.$label.err" | grep -vc 'incorrect function type')
        printf '%s\t%s\t%d\t%.2f\t%d\t%d\t%d\n' "$file" "$label" "$rc" "$(echo "$end - $start" | bc)" "$errors" "$warnings" "$sanitizer"
    }
    if [ "$tests" = 1 ]; then
        run gen-impl "$bin" gen-impl "$file" --dry-run "${inc_args[@]+"${inc_args[@]}"}" "${cfg[@]+"${cfg[@]}"}" --out "$out/gen"
        return
    fi
    for fe in $frontends; do
        case $fe in
            ts) run ts "$bin" gen-tests "$file" --dry-run "${inc_args[@]+"${inc_args[@]}"}" "${cfg[@]+"${cfg[@]}"}" --out "$out/gen" --frontend tree-sitter ;;
            cl) run cl "$bin" gen-tests "$file" --dry-run "${inc_args[@]+"${inc_args[@]}"}" "${cfg[@]+"${cfg[@]}"}" --out "$out/gen" --frontend libclang --std "$std" ;;
        esac
    done
    if [ -n "$dump" ]; then
        timeout "$limit" "$dump" --frontend tree-sitter "${dcfg[@]+"${dcfg[@]}"}" "$file" > "$out/$id.ts.model" 2> "$out/$id.ts.model.err"
        rc_ts=$?
        timeout "$limit" "$dump" --frontend libclang --std "$std" "${inc_args[@]+"${inc_args[@]}"}" "${dcfg[@]+"${dcfg[@]}"}" "$file" > "$out/$id.cl.model" 2> "$out/$id.cl.model.err"
        rc_cl=$?
        if [ $rc_ts -ne 0 ] || [ $rc_cl -ne 0 ]; then same=failed
        elif cmp -s "$out/$id.ts.model" "$out/$id.cl.model"; then same=same
        else same=differs; fi
        printf '%s\tmodels\t%s\t%d\t%d\n' "$file" "$same" "$(wc -l < "$out/$id.ts.model")" "$(wc -l < "$out/$id.cl.model")"
    fi
}
export -f one
export bin dump out std limit tests frontends inc config

xargs -P "$jobs" -I{} bash -c 'one "$1"' _ {} < "$list" | sort > "$out/summary.tsv"

# ---- the answer --------------------------------------------------------------------------------------------------

report="$out/report.txt"
{
    echo "corpus: $(wc -l < "$list") files, $( [ "$tests" = 1 ] && echo gen-impl || echo gen-tests ) with $bin"
    echo
    echo "== crashes, hangs, usage errors, sanitizer reports (must be none)"
    awk -F'\t' '$2 != "models" && ($3 > 3 || $7 > 0) { print $1 " [" $2 "] rc=" $3 " sanitizer=" $7 }' "$out/summary.tsv"
    echo
    echo "== slowest runs"
    awk -F'\t' '$2 != "models" { print $4 "\t" $1 " [" $2 "]" }' "$out/summary.tsv" | sort -rn | head -5
    echo
    echo "== messages of the errors, by how many files say them"
    for f in "$out"/*.err; do
        [ -e "$f" ] || continue
        grep ': error:' "$f" | sed -E 's/^[^:]*:[0-9]+:[0-9]+: error: //' | sort -u
    done | sort | uniq -c | sort -rn | head -40
    echo
    if [ "$tests" = 0 ]; then
        echo "== models that could not be made (the dumper failed or timed out; must be none)"
        awk -F'\t' '$2 == "models" && $3 == "failed" { print $1 }' "$out/summary.tsv"
        echo
        echo "== headers the two frontends read differently"
        awk -F'\t' '$2 == "models" && $3 == "differs" { print $1 "  (tree-sitter " $4 " lines, libclang " $5 " lines)" }' "$out/summary.tsv"
        echo
        echo "== counts"
        printf 'models equal: %d\nmodels different: %d\n' \
            "$(awk -F'\t' '$2 == "models" && $3 == "same"' "$out/summary.tsv" | wc -l)" \
            "$(awk -F'\t' '$2 == "models" && $3 == "differs"' "$out/summary.tsv" | wc -l)"
    fi
} > "$report"
cat "$report"
