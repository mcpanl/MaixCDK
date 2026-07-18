#!/bin/bash
set -e

# ===== 可配置参数 =====
SRC_IMG="./make_image/sophpi-duo-20260417-0947.img"
WORK_IMG="/tmp/working_image.img"
OUT_DIR="./make_image"

# 分发时若 boot1（p2，ext）空间不足：在复制镜像后向 .img 末尾追加空间，并扩展分区表中的 p2 + resize2fs。
# p1（FAT）起始扇区不变，仅拉长 p2，一般不影响从 boot0 启动。依赖：truncate、parted、e2fsck、resize2fs。
#
# 注意：默认 sudo 会 env_reset，「VAR=512 sudo ./new_image.sh」里的 VAR 往往传不进 root，扩容不会生效。
# 任选其一：
#   sudo ./new_image.sh --boot1-grow-mib 512
#   sudo env BOOT1_GROW_MIB=512 ./new_image.sh
#   sudo -E ./new_image.sh    # 若当前 shell 已 export BOOT1_GROW_MIB
BOOT1_GROW_MIB="${BOOT1_GROW_MIB:-0}"
while (( $# )); do
  case "$1" in
    --boot1-grow-mib|--boot1-grow)
      BOOT1_GROW_MIB="${2:?--boot1-grow-mib 需要数值（MiB）}"
      shift 2
      ;;
    -h|--help)
      cat <<'EOF'
用法: sudo ./new_image.sh [--boot1-grow-mib N]

  --boot1-grow-mib N   向镜像末尾追加 N MiB 并扩展 boot1（p2 ext）；推荐（sudo 下仍生效）。

环境变量（需配合 sudo env 或 sudo -E 传入 root）:
  BOOT1_GROW_MIB       同上，默认 0。

勿使用「BOOT1_GROW_MIB=512 sudo …」：多数系统 sudo 会清空环境，变量进不了脚本。
EOF
      exit 0
      ;;
    *)
      echo "未知参数: $1（可用 --help）" >&2
      exit 1
      ;;
  esac
done
if ! [[ "$BOOT1_GROW_MIB" =~ ^[0-9]+$ ]]; then
  echo "BOOT1_GROW_MIB 须为非负整数，当前: $BOOT1_GROW_MIB" >&2
  exit 1
fi

BOOT0_SRC="./make_image/boot0"
BOOT1_SRC="./make_image/boot1"

MOUNT_BOOT0="/mnt/boot0"
MOUNT_BOOT1="/mnt/boot1"
ZERO_FILL_FILENAME=".zero_fill_tmp"

LOOP_DEV=""
BOOT0_MOUNTED=0
BOOT1_MOUNTED=0

cleanup() {
  set +e

  if [ "$BOOT1_MOUNTED" -eq 1 ]; then
    umount "$MOUNT_BOOT1"
  fi

  if [ "$BOOT0_MOUNTED" -eq 1 ]; then
    umount "$MOUNT_BOOT0"
  fi

  if [ -n "$LOOP_DEV" ]; then
    losetup -d "$LOOP_DEV"
  fi
}

trap cleanup EXIT

# ===== 检查 root =====
if [ "$EUID" -ne 0 ]; then
  echo "请使用 sudo 运行"
  exit 1
fi

copy_tree_excluding_append() {
  local src_dir="$1"
  local dst_dir="$2"

  # 复制除 *.append 之外的所有内容；*.append 会在后续单独“追加”处理。
  # tar 能稳地保留目录结构。目标分区可能不支持 chown/chmod，
  # 因此提取时不保留 owner/permission，避免 "Operation not permitted"。
  (cd "$src_dir" && tar --exclude='*.append' -cf - .) | (cd "$dst_dir" && tar --no-same-owner --no-same-permissions -xf -)
}

apply_append_files() {
  local src_dir="$1"
  local dst_dir="$2"

  # 按字典序保证追加顺序可预期（当存在多个 .append 作用到同一目标文件时）。
  while IFS= read -r rel_path; do
    local src_file="$src_dir/$rel_path"
    local dst_rel_path="${rel_path%.append}" # 去掉尾部 ".append"
    local dst_file="$dst_dir/$dst_rel_path"

    mkdir -p "$(dirname "$dst_file")"

    if [ -e "$dst_file" ]; then
      # 将 .append 文件内容追加到目标文件末尾。
      cat "$src_file" >> "$dst_file"
    else
      # 目标文件不存在时，等价于对空文件追加。
      cp -p "$src_file" "$dst_file"
    fi
  done < <(cd "$src_dir" && find . -type f -name '*.append' -print | sed 's#^\./##' | sort)
}

# 打印已挂载分区的总容量、可用空间（MB，与 df -B1M 一致，按 1024 进制兆字节）
print_partition_df_mb() {
  local label="$1"
  local mp="$2"
  local total_mb avail_mb
  read -r total_mb avail_mb < <(df -B1M "$mp" | awk 'NR==2 {print $2+0, $4+0}')
  echo "  ${label}: 总容量 ${total_mb} MB, 可用 ${avail_mb} MB"
}

# 目录存在则返回磁盘占用字节数，否则 0
du_dir_bytes() {
  local d="$1"
  if [ -d "$d" ]; then
    du -sb "$d" 2>/dev/null | awk '{print $1+0}'
  else
    echo 0
  fi
}

# 字节 -> MB，保留一位小数
bytes_to_mb_one_decimal() {
  awk -v b="$1" 'BEGIN { printf "%.1f\n", b / 1024 / 1024 }'
}

zero_fill_free_space() {
  local mount_point="$1"
  local zero_file="${mount_point}/${ZERO_FILL_FILENAME}"

  echo "开始零填充剩余空间: ${mount_point}"

  rm -f "$zero_file"

  # 预期会在空间写满时退出非 0，这里只把“写满”当作正常流程的一部分。
  if dd if=/dev/zero of="$zero_file" bs=16M status=progress conv=fsync; then
    :
  else
    echo "零填充已写满剩余空间，开始删除临时文件"
  fi

  sync
  rm -f "$zero_file"
  sync

  echo "零填充完成并已清理临时文件: ${mount_point}"
}

echo "==== 1. 准备镜像 ===="
cp "$SRC_IMG" "$WORK_IMG"

if (( BOOT1_GROW_MIB > 0 )); then
  echo "==== 1b. 扩容镜像文件与 boot1（p2）分区（+${BOOT1_GROW_MIB} MiB）===="
  truncate -s "+${BOOT1_GROW_MIB}M" "$WORK_IMG"
fi

echo "==== 2. 创建 loop 设备 ===="
LOOP_DEV=$(losetup -Pf --show "$WORK_IMG")
echo "使用设备: $LOOP_DEV"

# 等待分区识别
sleep 1

if (( BOOT1_GROW_MIB > 0 )); then
  echo "==== 2b. 扩展 MBR 中 p2 至磁盘末尾并拉大 ext 文件系统 ===="
  parted -s "$LOOP_DEV" resizepart 2 100%
  e2fsck -f -y "${LOOP_DEV}p2"
  resize2fs "${LOOP_DEV}p2"
fi

echo "==== 3. 挂载分区 ===="
mkdir -p "$MOUNT_BOOT0"
mkdir -p "$MOUNT_BOOT1"

mount "${LOOP_DEV}p1" "$MOUNT_BOOT0"
BOOT0_MOUNTED=1
mount "${LOOP_DEV}p2" "$MOUNT_BOOT1"
BOOT1_MOUNTED=1

echo "==== 3b. 镜像分区容量（挂载后，写入前）===="
print_partition_df_mb "boot0 (${LOOP_DEV}p1 → ${MOUNT_BOOT0})" "$MOUNT_BOOT0"
print_partition_df_mb "boot1 (${LOOP_DEV}p2 → ${MOUNT_BOOT1})" "$MOUNT_BOOT1"

BOOT0_APPS="${BOOT0_SRC}/maixapp/apps"
BOOT1_APPS="${BOOT1_SRC}/maixapp/apps"
b0_bytes="$(du_dir_bytes "$BOOT0_APPS")"
b1_bytes="$(du_dir_bytes "$BOOT1_APPS")"
total_apps_bytes=$((b0_bytes + b1_bytes))
echo "==== 3c. 源目录 maixapp/apps 占用（将随 tar 写入对应分区）===="
echo "  ${BOOT0_APPS}: $(bytes_to_mb_one_decimal "$b0_bytes") MB"
echo "  ${BOOT1_APPS}: $(bytes_to_mb_one_decimal "$b1_bytes") MB"
echo "  合计: $(bytes_to_mb_one_decimal "$total_apps_bytes") MB"

echo "==== 4. 覆盖 boot0（支持 .append 追加） ===="
copy_tree_excluding_append "$BOOT0_SRC" "$MOUNT_BOOT0"
apply_append_files "$BOOT0_SRC" "$MOUNT_BOOT0"

echo "==== 5. 覆盖 boot1（支持 .append 追加） ===="
copy_tree_excluding_append "$BOOT1_SRC" "$MOUNT_BOOT1"
apply_append_files "$BOOT1_SRC" "$MOUNT_BOOT1"

echo "==== 6. boot1 零填充空闲空间 ===="
zero_fill_free_space "$MOUNT_BOOT1"

sync

echo "==== 7. 卸载 ===="
umount "$MOUNT_BOOT0"
BOOT0_MOUNTED=0
umount "$MOUNT_BOOT1"
BOOT1_MOUNTED=0

echo "==== 8. 释放 loop 设备 ===="
losetup -d "$LOOP_DEV"
LOOP_DEV=""

echo "==== 9. 重命名镜像并生成校验文件 ===="
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
FINAL_IMG="${OUT_DIR}/sophpi-duo-${TIMESTAMP}.img"
FINAL_SHA256="${FINAL_IMG}.sha256"
FINAL_TAR_XZ="${FINAL_IMG}.tar.xz"
FINAL_TAR_XZ_SHA256="${FINAL_TAR_XZ}.sha256"

mkdir -p "$OUT_DIR"
mv "$WORK_IMG" "$FINAL_IMG"
sha256sum "$FINAL_IMG" > "$FINAL_SHA256"

echo "==== 10. 生成 tar.xz 压缩包并生成校验文件 ===="
tar -C "$OUT_DIR" -cJf "$FINAL_TAR_XZ" "$(basename "$FINAL_IMG")"
sha256sum "$FINAL_TAR_XZ" > "$FINAL_TAR_XZ_SHA256"

echo "==== 完成 ===="
echo "新镜像: $FINAL_IMG"
echo "SHA256: $FINAL_SHA256"
echo "压缩包: $FINAL_TAR_XZ"
echo "压缩包 SHA256: $FINAL_TAR_XZ_SHA256"
