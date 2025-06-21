#!/bin/bash

WATCH_FILE="src/main.cpp"  # 替换成你要监控的文件

while true; do
  inotifywait -e close_write "$WATCH_FILE"
  echo "文件被修改，开始构建并拷贝..."
  maixcdk build2 && scp -r dist/piano_camera_release/piano_camera root@192.168.100.198:/root/dist/piano_camera_release/piano_camera
done

