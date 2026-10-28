#!/usr/bin/env bash
# 读 StickS3 串口输出 N 秒（默认只读，不动 DTR/RTS）。
#   --reset : 先按 USB-Serial-JTAG 时序进下载模式再读，用于确认是否停在 bootloader
#   注意：DTR 对应 IO0、RTS 对应 EN；乱动这两个线会把板子送进 bootloader 而不是重启它。
# 用法: ./tools/monitor.sh [--reset] [秒数] [端口]
set -euo pipefail

# 解释器用 PlatformIO 自己的 venv（uv tool 安装），它带 pyserial。
PY="$(uv tool dir 2>/dev/null || true)/platformio/bin/python"
[ -x "$PY" ] || { echo "找不到 PlatformIO 解释器: $PY（先执行 uv tool install platformio）" >&2; exit 1; }

DO_RESET=0
if [ "${1:-}" = "--reset" ]; then DO_RESET=1; shift; fi
SECS="${1:-8}"
PORT="${2:-/dev/ttyACM0}"

exec "$PY" - "$DO_RESET" "$SECS" "$PORT" <<'PYEOF'
import sys, time, serial

do_reset = sys.argv[1] == "1"
secs = float(sys.argv[2])
port = sys.argv[3]


def open_port():
    return serial.Serial(port, 115200, timeout=0.2)


if do_reset:
    s = open_port()
    s.dtr = False; s.rts = True; time.sleep(0.1)
    s.dtr = True;  s.rts = False; time.sleep(0.05)
    s.dtr = False
    s.close()
    time.sleep(0.5)

s = None
for _ in range(30):
    try:
        s = open_port()
        break
    except serial.SerialException:
        time.sleep(0.1)
if s is None:
    sys.exit("端口打开失败")

deadline = time.time() + secs
while time.time() < deadline:
    try:
        data = s.read(4096)
    except serial.SerialException:
        time.sleep(0.2)
        continue
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
PYEOF
