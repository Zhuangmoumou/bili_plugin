#!/bin/bash

echo "编译主服务"
cd main
CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o ../server

echo "编译短信服务"
cd ../sms
CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o ../bili-sms
