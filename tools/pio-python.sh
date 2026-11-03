#!/usr/bin/env bash
# 打印一个可用的 Python 解释器路径（要求能 import serial），供 monitor.sh 与 esptool.sh 使用。
# 查找顺序：
#   1. $PLATFORMIO_PYTHON
#   2. pio 脚本 shebang 指向的解释器（uv / pipx 安装的 pio 都指向自己的 venv）
#   3. PlatformIO 默认 core 目录下的 penv
#   4. 系统 python3
# 找不到就报错退出，不打印任何路径。
set -euo pipefail

candidate="${PLATFORMIO_PYTHON:-}"

if [ -z "$candidate" ]; then
  pio_bin="$(command -v pio || true)"
  if [ -n "$pio_bin" ]; then
    shebang="$(head -n 1 "$pio_bin" 2>/dev/null || true)"
    case "$shebang" in
      '#!'*)
        sh_path="${shebang#\#\!}"
        sh_path="${sh_path%% *}"
        [ -x "$sh_path" ] && candidate="$sh_path"
        ;;
    esac
  fi
fi

if [ -z "$candidate" ]; then
  core="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
  [ -x "$core/penv/bin/python" ] && candidate="$core/penv/bin/python"
fi

if [ -z "$candidate" ]; then
  candidate="$(command -v python3 || true)"
fi

[ -n "$candidate" ] && [ -x "$candidate" ] || {
  echo "找不到 Python 解释器。请安装 PlatformIO Core，或设置 PLATFORMIO_PYTHON=/path/to/python" >&2
  exit 1
}

"$candidate" -c 'import serial' 2>/dev/null || {
  echo "解释器 $candidate 缺少 pyserial。请安装 PlatformIO Core，或设置 PLATFORMIO_PYTHON 指向带 pyserial 的解释器" >&2
  exit 1
}

printf '%s\n' "$candidate"