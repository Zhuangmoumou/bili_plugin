#!/bin/bash

set -euo pipefail

xmake f -c

# 设置qt路径
xmake f --qt="/home/zhuang/program/qt" --arch=arm64-v8a --toolchain=zig --cross=aarch64-linux-gnu.2.27 -m release -vD

xmake

./go_server/build.sh
