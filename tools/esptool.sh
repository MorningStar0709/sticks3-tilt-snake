#!/usr/bin/env bash
# 包装 PlatformIO 自带的 esptool：解释器与包路径都取自 PlatformIO 默认位置。
# esptool 依赖 PlatformIO venv 里的 pyserial，不要换成系统 python。
# 用法: tools/esptool.sh --chip esp32s3 --port /dev/ttyACM0 chip_id
set -euo pipefail

core="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
esptool="$core/packages/tool-esptoolpy/esptool.py"
py="$(uv tool dir 2>/dev/null || true)/platformio/bin/python"

[ -f "$esptool" ] || { echo "找不到 esptool: $esptool（先在本项目跑一次 pio run）" >&2; exit 1; }
[ -x "$py" ] || { echo "找不到 PlatformIO 解释器: $py（先执行 uv tool install platformio）" >&2; exit 1; }

exec "$py" "$esptool" "$@"
