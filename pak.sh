#!/bin/bash

echo '编译Go服务器'
cd go_server
CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o server
cd ..
echo '----------------'

# 临时文件
mkdir bili_plugin
cp go_server/server ./bili_plugin
cp build/linux/arm64-v8a/release/libbili_plugin.so ./bili_plugin
cp -r ./qml ./bili_plugin
cp metadata.json ./bili_plugin
cp icon.png ./bili_plugin
cp -r FFmpegPlayer ./bili_plugin

# 打包
zip -r bili_plugin.zip bili_plugin/

# 清除

rm -r ./bili_plugin

echo '----------------'
echo '打包完成'
