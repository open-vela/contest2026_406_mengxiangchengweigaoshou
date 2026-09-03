#!/usr/bin/env bash
# OpenWave 板级适配补丁一键应用脚本
# 在 openvela 工作区根目录执行，例如：
#   bash ~/openvela/contest2026_406_mengxiangchengweigaoshou/apply_patches.sh
# 或
#   OPENVELA_ROOT=~/openvela bash <路径>/apply_patches.sh
set -e

ROOT="${OPENVELA_ROOT:-$HOME/openvela}"
REPO="$ROOT/contest2026_406_mengxiangchengweigaoshou"

apply_one()
{
  local dir="$1"
  local patch="$2"

  if [ ! -d "$ROOT/$dir/.git" ]; then
    echo "skip $dir (no git checkout under $ROOT)"
    return 0
  fi

  if git -C "$ROOT/$dir" apply --check "$patch" 2>/dev/null; then
    git -C "$ROOT/$dir" apply "$patch"
    echo "applied: $patch"
  elif git -C "$ROOT/$dir" apply --reverse --check "$patch" 2>/dev/null; then
    echo "already applied: $patch"
  else
    echo "WARN: cannot apply $patch (tree may differ)"
  fi
}

echo "== OpenWave patches =="
apply_one nuttx "$REPO/logs/patch_nuttx.diff"
apply_one vendor/st "$REPO/logs/patch_vendor_st.diff"
echo "done"
