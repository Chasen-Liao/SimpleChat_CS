#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_CLIENTS 10      // 最大连接用户数
#define BUFFER_SIZE 1024    // 缓冲区大小
#define NAME_SIZE 20        // 用户名最大字符数
#define PORT 8088           // 服务器端口

// 消息类型定义
typedef enum {
    MSG_REGISTER = 1,       // 注册消息 (username)
    MSG_PUBLIC_CHAT = 2,    // 公共聊天 (message)
    MSG_PRIVATE_CHAT = 3,   // 私聊 (target:message)
    MSG_USER_LIST = 4,      // 用户列表请求/响应 (user1,user2,...)
    MSG_DISCONNECT = 5,     // 断开连接
    MSG_USER_JOIN = 6,      // 用户加入通知 (username)
    MSG_USER_LEAVE = 7,     // 用户离开通知 (username)
    MSG_SYSTEM = 8          // 系统消息
} MessageType;

// 通信协议包结构
typedef struct {
    MessageType type;           // 消息类型
    int length;                 // 消息体长度
    char sender[NAME_SIZE];     // 发送者名称
    char data[BUFFER_SIZE];     // 消息数据
} ChatPacket;

// 客户端结构（用于epoll模型）
typedef struct {
    char name[NAME_SIZE];       // 客户端名称
    int sockfd;                 // 套接字文件描述符
    int logged_in;              // 是否已登录
    time_t login_time;          // 登录时间
} ClientInfo;

// 打包函数
void pack_message(ChatPacket *packet, MessageType type, const char *sender, const char *data);

// 解包函数
void unpack_message(ChatPacket *packet, MessageType *type, char *sender, char *data);

// 获取客户端列表字符串
void get_client_list_string(char *list_str, int max_len);

#endif // PROTOCOL_H
