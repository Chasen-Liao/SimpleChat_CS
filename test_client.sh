#!/bin/bash

# 测试脚本：启动多个客户端进行功能测试

echo "启动客户端1..."
(
sleep 1
echo "Alice"
sleep 2
echo "大家好！我是Alice"
sleep 1
echo "/list"
sleep 1
echo "/help"
sleep 2
) | ./client/client &

sleep 3

echo "启动客户端2..."
(
sleep 1
echo "Bob"
sleep 2
echo "Hello, 我是Bob"
sleep 1
echo "/list"
sleep 1
echo "/pm Alice 你好Alice，我是Bob"
sleep 2
) | ./client/client &

sleep 5

echo "启动客户端3..."
(
sleep 1
echo "Charlie"
sleep 2
echo "/list"
sleep 1
echo "公开消息：大家好！"
sleep 2
echo "/pm Alice 私密消息：你好Alice"
sleep 2
) | ./client/client &

wait
