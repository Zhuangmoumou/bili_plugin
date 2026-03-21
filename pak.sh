#!/bin/bash

echo '编译Go服务器'
cd go_server
CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o server
echo '----------------'
# 打包
cd ..
zip -r bili_plugin.zip go_server/server qml/ metadata.json build/linux/arm64-v8a/release/libbili_plugin.so FFmpegPlayer/ icon.png

echo '----------------'
echo '打包完成'
