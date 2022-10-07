#! /bin/sh

# "${1}/wake-format" $(cat ./srclist.txt) > /dev/null

BASEDIR=$(dirname $0)
WAKE_ROOT="$BASEDIR/../../.."

"${1}/wake-format" "$WAKE_ROOT/extensions/vscode/vscode.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/shim-wake/shim.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/job-cache/job-cache.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/bsp-wake/bsp.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/wake/wake.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/fuse-waked/fuse-waked.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/lsp-wake/lsp.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/wake-format/wake-format.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/wake-unit/wake-unit.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/wakebox/wakebox.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tools/tools.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/runtime/symlinks/test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/standard-library/groupBy/test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/parser/unicode/test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/type-system/require-pass/test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/type-system/match-pass/test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/type-system/extract-pass/test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/type-system/lambda-pass/test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/type-system/value-restriction/test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/standard-library/json-normalize-merge/json-test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/versions/v0_27.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/versions/v0_24.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/versions/v0_28.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/nothing/nothing.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/option.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/syntax.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/integer.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/regexp.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/types.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/boolean.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/result.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/tuple.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/double.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/order.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/print.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/system/environment.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/gcc_wake/pkgconfig.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/gcc_wake/gcc.wake > /dev/null" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/blake2/blake2.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/utf8proc/utf8proc.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/fuse.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/whereami/whereami.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/gopt/gopt.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/lemon/lemon.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/sqlite3.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/gmp.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/re2.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/vendor.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/emscripten.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/git.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/ncurses.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/siphash/siphash.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/build.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/compat/compat.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/json/json.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/dst/dst.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/parser/parser.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/optimizer/optimizer.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/types/types.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/runtime/runtime.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/wcl/wcl.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/src.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/util/util.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/src/wakefs/wakefs.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/vendor/re2c.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/type-system/multi-match-pass/test.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/tests/tests.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/map.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/vector.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/string.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/system/sources.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/system/path.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/json.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/list.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/core/tree.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/system/job.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/system/incremental.wake" > /dev/null
"${1}/wake-format" "$WAKE_ROOT/share/wake/lib/system/io.wake" > /dev/null
