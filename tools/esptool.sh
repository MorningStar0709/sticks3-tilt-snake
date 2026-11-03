#!/usr/bin/env bash
# 包装 PlatformIO 自带的 esptool：esptool.py 取自 PlatformIO core 目录，
# 解释器由 tools/pio-python.sh 解析（它保证能 import serial）。
# 用法: tools/esptool.sh --chip esp32s3 --port /dev/ttyACM0 chip_id
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
core="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
esptool="$core/packages/tool-esptoolpy/esptool.py"
py="$("$here/pio-python.sh")"

[ -f "$esptool" ] || { echo "找不到 esptool: $esptool（先在本项目跑一次 pio run）" >&2; exit 1; }

exec "$py" "$esptool" "$@"
