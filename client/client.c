// client.c - 支持公聊、私聊、在线列表的客户端
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../include/protocol.h"

#define SEVER_IP "127.0.0.1"    // 服务器端IP地址

// 全局变量：连接状态标志
volatile int connection_closed = 0;  // 0=连接中，1=连接已断开
pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;
int global_sockfd = -1;

// 前置声明
void *receive_messages(void *socketfd_p);
void send_command_to_server(int sockfd, MessageType type, const char *data);

// 显示帮助信息
void show_help(void) {
    printf("\n╔═══════════════════════════════════════╗\n");
    printf("║       🎮 聊天室命令帮助              ║\n");
    printf("╠═══════════════════════════════════════╣\n");
    printf("║ /list  - 显示在线用户列表            ║\n");
    printf("║ /pm <用户名> <消息> - 发送私聊      ║\n");
    printf("║         例: /pm Alice 你好           ║\n");
    printf("║ /help  - 显示此帮助信息              ║\n");
    printf("║ /exit  - 退出聊天室                  ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");
}

// 发送命令到服务器
void send_command_to_server(int sockfd, MessageType type, const char *data) {
    ChatPacket packet;
    pack_message(&packet, type, "", data);
    send(sockfd, &packet, sizeof(ChatPacket), 0);
}

int main(void)
{
    char buf[BUFFER_SIZE];
    char name[NAME_SIZE];
    
    printf("╔═══════════════════════════════════════╗\n");
    printf("║      🎬 欢迎进入聊天室 v2.0           ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");
    
    printf("请输入用户名: ");
    fflush(stdout);
    fgets(name, NAME_SIZE, stdin);
    if (strlen(name) > 0 && name[strlen(name) - 1] == '\n') {
        name[strlen(name) - 1] = '\0';
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Failed to create socket");
        return -1;
    }
    
    struct sockaddr_in addr;
    addr.sin_port = htons(PORT);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(SEVER_IP);
    
    printf("正在连接到服务器 %s:%d...\n", SEVER_IP, PORT);
    fflush(stdout);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("❌ 连接失败");
        close(sockfd);
        return -1;
    }
    
    printf("✓ 已连接到服务器！\n\n");
    global_sockfd = sockfd;
    
    // 创建接收线程
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_messages, &sockfd);
    pthread_detach(recv_thread);
    
    // 发送注册消息
    send_command_to_server(sockfd, MSG_REGISTER, name);
    sleep(1);  // 等待服务器确认
    
    show_help();
    
    printf("💬 输入消息或命令 (输入 '/help' 查看帮助):\n");
    printf("> ");
    fflush(stdout);
    
    // 主循环：获取用户输入并发送消息
    while (1)
    {
        pthread_mutex_lock(&connection_mutex);
        if (connection_closed)
        {
            pthread_mutex_unlock(&connection_mutex);
            break;
        }
        pthread_mutex_unlock(&connection_mutex);
        
        memset(buf, 0, BUFFER_SIZE);
        if (fgets(buf, BUFFER_SIZE, stdin) == NULL)
            break;
        
        if (buf[strlen(buf) - 1] == '\n')
            buf[strlen(buf) - 1] = '\0';
        
        if (strlen(buf) == 0)
        {
            printf("> ");
            fflush(stdout);
            continue;
        }
        
        // 处理命令
        if (buf[0] == '/')
        {
            if (strcmp(buf, "/exit") == 0)
            {
                printf("👋 正在退出...\n");
                send_command_to_server(sockfd, MSG_DISCONNECT, "");
                break;
            }
            else if (strcmp(buf, "/list") == 0)
            {
                send_command_to_server(sockfd, MSG_USER_LIST, "");
            }
            else if (strcmp(buf, "/help") == 0)
            {
                show_help();
            }
            else if (strncmp(buf, "/pm ", 4) == 0)
            {
                // 格式: /pm username message
                char *space = strchr(buf + 4, ' ');
                if (space != NULL)
                {
                    *space = '\0';
                    char *target = buf + 4;
                    char *message = space + 1;
                    
                    // 构建私聊格式: "target:message"
                    char pm_data[BUFFER_SIZE];
                    snprintf(pm_data, BUFFER_SIZE, "%s:%s", target, message);
                    send_command_to_server(sockfd, MSG_PRIVATE_CHAT, pm_data);
                }
                else
                {
                    printf("✗ 命令格式错误，请使用: /pm <用户名> <消息>\n");
                }
            }
            else
            {
                printf("✗ 未知命令: %s\n", buf);
            }
        }
        else
        {
            // 发送公聊消息
            send_command_to_server(sockfd, MSG_PUBLIC_CHAT, buf);
        }
        
        printf("> ");
        fflush(stdout);
    }
    
    close(sockfd);
    return 0;
}

// 接收消息的线程函数
void *receive_messages(void *socketfd_p)
{
    int sockfd = *((int *)socketfd_p);
    ChatPacket packet;
    int bytes_read;
    
    while ((bytes_read = recv(sockfd, &packet, sizeof(ChatPacket), 0)) > 0)
    {
        char sender[NAME_SIZE];
        char data[BUFFER_SIZE];
        MessageType type;
        
        unpack_message(&packet, &type, sender, data);
        
        switch (type)
        {
            case MSG_PUBLIC_CHAT:
                printf("\r[%s]: %s\n> ", sender, data);
                fflush(stdout);
                break;
                
            case MSG_PRIVATE_CHAT:
                printf("\r💬 %s\n> ", data);
                fflush(stdout);
                break;
                
            case MSG_SYSTEM:
                printf("\r[系统] %s\n> ", data);
                fflush(stdout);
                break;
                
            case MSG_USER_LIST:
                printf("\r📋 在线用户: %s\n> ", data);
                fflush(stdout);
                break;
                
            default:
                printf("\r接收: %s\n> ", data);
                fflush(stdout);
                break;
        }
    }
    
    if (bytes_read == 0)
    {
        printf("\n❌ 服务器已断开连接！\n");
    }
    else if (bytes_read < 0)
    {
        perror("❌ 接收数据失败");
    }
    
    pthread_mutex_lock(&connection_mutex);
    connection_closed = 1;
    pthread_mutex_unlock(&connection_mutex);
    
    printf("程序即将退出...\n");
    fflush(stdout);
    
    pthread_exit(NULL);
}