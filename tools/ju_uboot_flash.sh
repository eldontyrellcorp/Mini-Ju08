#!/bin/bash
# Flash Mini-Ju08 into an FM-1 that is in the chip's USB boot mode (WL80UBOOT, 4c4a:8057), from Linux, without the
# update entry of the running firmware. Uses kagaimiq/jl-uboot-tool (pinned), which uploads a small loader to RAM and
# then reads and writes the flash through SCSI commands.
#
# What it does, in order:
#   1. builds the app area [0x4000, 0x93000) from the package and checks it (tools/fm1_app_area.py);
#   2. finds the FM-1 (SCSI vendor WL82) and reads the WHOLE flash into a backup file: nothing is written yet;
#   3. asks you to type YES; then erases and writes only the app area (what the update loader writes: never the head,
#      the boot code and chip configuration at [0, 0x4000), never the user store), reads it back and compares;
#   4. resets the chip so it boots the new firmware.
# If anything fails before step 3 nothing has been changed. If it fails during 3 the app area is incomplete: the chip
# stays in (or falls back to) boot mode and you run this again.
#
# Put the FM-1 in boot mode first: with it running, hold OCT- and OCT+ for 5 seconds (the screen says UBOOT).
#   tools/ju_uboot_flash.sh [PACKAGE.fwsc]       (default build/felucca.fwsc)
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PKG="${1:-$HERE/build/felucca.fwsc}"
JOB="${JU_FLASH_DIR:-$HOME/.cache/ju08-uboot-flash}"
TOOL_REPO=https://github.com/kagaimiq/jl-uboot-tool.git
TOOL_COMMIT=adb3f18889e88ac512ce0a3c4d8cc3d3cb30696a
mkdir -p "$JOB"

echo "== 1. the app area"
python3 -I "$HERE/tools/fm1_app_area.py" "$PKG" "$HERE/build/felucca.bin" "$JOB/app_area.bin"
[ "$(stat -c %s "$JOB/app_area.bin")" = 585728 ] || { echo "unexpected app area size"; exit 1; }

echo "== 2. the flashing tool (pinned to $TOOL_COMMIT)"
if [ ! -d "$JOB/jl-uboot-tool/.git" ]; then git clone -q "$TOOL_REPO" "$JOB/jl-uboot-tool"; fi
git -C "$JOB/jl-uboot-tool" checkout -q "$TOOL_COMMIT"
if [ ! -x "$JOB/venv/bin/python" ]; then
  python3 -m venv "$JOB/venv"
  "$JOB/venv/bin/pip" install -q crcmod pycryptodomex tqdm pyyaml
fi

echo "== 3. the FM-1 in boot mode"
DEV=""
for d in /sys/class/scsi_generic/sg*; do
  v="$(tr -d ' \n' < "$d/device/vendor" 2>/dev/null || true)"
  m="$(tr -d ' \n' < "$d/device/model" 2>/dev/null || true)"
  if [ "$v" = "WL82" ] && [ "$m" = "UBOOT1.00" ]; then DEV="/dev/$(basename "$d")"; fi
done
if [ -z "$DEV" ]; then
  echo "No WL82 UBOOT1.00 device. Hold OCT- and OCT+ on the FM-1 for 5 s until the screen says UBOOT, then run this again."
  echo "(lsusb should list 4c4a:8057 WL80UBOOT1.00)"
  exit 2
fi
echo "found $DEV"
run() { (cd "$JOB/jl-uboot-tool" && sudo "$JOB/venv/bin/python" jluboottool.py --device "$DEV" --chip wl82 "$@"); }

echo "== 4. backup of the whole flash (read only)"
BK="$JOB/backup-$(date +%Y%m%d-%H%M%S).bin"
run "read 0x0 0x100000 $BK"
sudo chown "$(id -u):$(id -g)" "$BK"
[ "$(stat -c %s "$BK")" = 1048576 ] || { echo "the backup is not 1 MiB: stopping, nothing was written"; exit 1; }
if [ -z "$(tr -d '\377' < "$BK" | head -c 16)" ] || [ -z "$(tr -d '\000' < "$BK" | head -c 16)" ]; then
  echo "the backup is all 0xFF or all 0x00: the read does not work, stopping, nothing was written"; exit 1
fi
cmp -s <(head -c 16384 "$BK" | head -c 4) <(printf '\377\377\377\377') && { echo "the head of the flash looks erased: stopping"; exit 1; }
echo "backup: $BK  ($(sha256sum "$BK" | cut -c1-16)...)"
cmp -n 585728 <(tail -c +16385 "$BK" | head -c 585728) "$JOB/app_area.bin" >/dev/null 2>&1 && echo "note: the flash already holds this app area"

read -r -p "Write the app area to flash 0x4000..0x93000 now? Type YES: " ans
[ "$ans" = YES ] || { echo "not written"; exit 0; }

echo "== 5. write"
run "write 0x4000 $JOB/app_area.bin"
echo "== 6. verify"
RB="$JOB/readback.bin"
run "read 0x4000 0x8F000 $RB"
sudo chown "$(id -u):$(id -g)" "$RB"
if cmp -s "$RB" "$JOB/app_area.bin"; then echo "verified: the flash holds the new app area"; else echo "VERIFY FAILED: run this script again (the chip stays in boot mode)"; exit 1; fi
echo "== 7. reset"
run "reset 1" || true
echo "Done. The FM-1 should boot Mini-Ju08 (a few seconds). The backup is $BK"
