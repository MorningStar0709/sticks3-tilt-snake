#!/usr/bin/env bash
# 一次 download mode 会话内完成：全片备份 → 烧写自检固件。
# 背景：板上 UiFlow2 用的 TinyUSB CDC（303a:832b），esptool 无法自动复位，
#       需要人工按下 reset 进入 bootloader；备份与烧写共用这一次人工动作。
#
# 用法:
#   ./tools/bringup.sh              # 备份（若尚无备份）后烧写
#   ./tools/bringup.sh --backup     # 只备份
#   ./tools/bringup.sh --port /dev/ttyACM1
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/sticks3-play"
ESPTOOL="$ROOT/.pio-core/packages/tool-esptoolpy/esptool.py"
PY="$ROOT/.venv/bin/python"
FLASH_SIZE=0x800000

PORT=/dev/ttyACM0
BACKUP_ONLY=0
while [ $# -gt 0 ]; do
  case "$1" in
    --backup) BACKUP_ONLY=1 ;;
    --port) PORT="${2:?--port 需要参数}"; shift ;;
    -h|--help) sed -n '1,9p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "未知参数: $1" >&2; exit 2 ;;
  esac
  shift
done

[ -c "$PORT" ] || { echo "找不到串口 $PORT，先插上板子" >&2; exit 1; }

# 只认"完整且可校验"的备份：大小必须等于整片容量，且有通过的 sha256 校验
latest_backup() {
  local f size
  f="$(ls -1t "$ROOT"/backups/*.bin 2>/dev/null | head -1 || true)"
  [ -n "$f" ] || return 0
  size="$(stat -c %s "$f" 2>/dev/null || echo 0)"
  if [ "$size" -ne "$((FLASH_SIZE))" ]; then
    echo "== 忽略不完整备份（${size} 字节，应为 $((FLASH_SIZE))）: $f" >&2
    return 0
  fi
  if [ -f "$f.sha256" ] && ! (cd "$(dirname "$f")" && sha256sum -c "$(basename "$f").sha256" >/dev/null 2>&1); then
    echo "== 忽略校验失败的备份: $f" >&2
    return 0
  fi
  printf '%s\n' "$f"
}

BACKUP="$(latest_backup)"
if [ -n "$BACKUP" ]; then
  echo "== 已有备份，跳过 dump: $BACKUP"
else
  mkdir -p "$ROOT/backups"
  BACKUP="$ROOT/backups/sticks3-uiflow2-$(date +%Y%m%d-%H%M%S).bin"
  echo "== 备份全片 $FLASH_SIZE 字节 → $BACKUP"
  # 先写 .part，成功后再改名：中途中断不会留下一个"看起来像备份"的残片
  if ! "$PY" "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 \
      --before default_reset --after no_reset \
      read_flash 0x0 "$FLASH_SIZE" "$BACKUP.part"; then
    rm -f "$BACKUP.part"
    echo "备份失败，未生成有效备份" >&2
    exit 1
  fi
  mv "$BACKUP.part" "$BACKUP"
  # 记录相对路径，便于整棵目录搬移后仍能校验
  (cd "$(dirname "$BACKUP")" && sha256sum "$(basename "$BACKUP")" > "$(basename "$BACKUP").sha256")
  cat "$BACKUP.sha256"
fi

if [ "$BACKUP_ONLY" = "1" ]; then
  echo "== 仅备份，结束。设备仍停留在 bootloader，可继续手动烧写。"
  exit 0
fi

echo "== 烧写自检固件"
cd "$PROJECT"
"$ROOT/bin/pio" run -t upload --upload-port "$PORT"
