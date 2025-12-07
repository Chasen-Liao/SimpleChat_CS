#!/bin/bash
# 演示脚本：展示epoll、私聊、在线列表功能

cd /home/chasen/lcz/learn_linux/linux应用开发/class/SimpleChat_CS

echo "=== 聊天室功能演示 ==="
echo ""
echo "1. 启动第一个客户端 (Alice)"
(
  sleep 0.5
  echo "Alice"
  sleep 1
  echo "大家好，我是Alice！"
  sleep 2
  echo "/list"
  sleep 2
  echo "等待Bob加入..."
  sleep 5
) | timeout 20 ./client/client &
PID1=$!

sleep 2

echo ""
echo "2. 启动第二个客户端 (Bob)"
(
  sleep 0.5
  echo "Bob"
  sleep 1
  echo "Hi Alice, 我是Bob"
  sleep 2
  echo "/list"
  sleep 1
  echo "/pm Alice 这是一条私密消息"
  sleep 3
) | timeout 20 ./client/client &
PID2=$!

sleep 3

echo ""
echo "3. 启动第三个客户端 (Charlie)"
(
  sleep 0.5
  echo "Charlie"
  sleep 1
  echo "/list"
  sleep 1
  echo "大家好，我是Charlie"
  sleep 3
) | timeout 20 ./client/client &
PID3=$!

# 等待所有客户端完成
wait $PID1 $PID2 $PID3

echo ""
echo "=== 演示完成 ==="
