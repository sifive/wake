#!/bin/bash
WAKE_BIN_DIR="$1"

# It's not valid to call wakebox with an empty PATH.
# So we fill PATH with some typical values.
export PATH="/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:$WAKE_BIN_DIR"

set -ex

# Preserve original directory location
INPUT_JSON="input.json"
RESULT_JSON="result.json"
TMPDIR="tmp"
mkdir tmp

# Remove temp build dir on error or exit.
cleanup () {
    # Sleep in hopes that fuse will close the session in time
    sleep 3
    rm -rf "$TMPDIR" 
    echo "?: $?"
}
trap cleanup EXIT

# Populate and create the test squashfs file
SQUASH_ROOT_DIR="$TMPDIR/squashfs-root"
SQUASHFS_FILENAME="$TMPDIR/pass.squashfs"

mkdir -p "$SQUASH_ROOT_DIR/.wakebox"
printf '#!/bin/sh\nexit 97\n' > "$SQUASH_ROOT_DIR/foo.sh"
chmod +x "$SQUASH_ROOT_DIR/foo.sh"
echo "/tmp/squashfs" > "$SQUASH_ROOT_DIR/.wakebox/mountpoint"

mksquashfs "$SQUASH_ROOT_DIR" "$SQUASHFS_FILENAME" -comp xz -all-root -noappend -no-xattrs

tree

# Run the test
"$WAKE_BIN_DIR/wakebox" -p "$INPUT_JSON" -o "$RESULT_JSON" --isolate-retcode

# Check result
[ "$(jq .usage.status "$RESULT_JSON")" -ne 97 ] && exit 98

# Cleanup
rm $RESULT_JSON

exit 0
