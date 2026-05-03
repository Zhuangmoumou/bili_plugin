#!/bin/bash

# 获取脚本所在目录并进入
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "编译主服务"
cd main
CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o ../server

echo "编译短信服务"
cd ../sms
CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o ../bili-sms
