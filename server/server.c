// Server.c - 使用epoll的多路IO聊天服务器
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <signal.h>
#include "../include/protocol.h"

#define MAX_EVENTS 64

// 全局客户端数组和计数
ClientInfo *clients[MAX_CLIENTS];
int client_count = 0;
FILE *log_fp = NULL;
time_t server_start_time = 0;
char log_file_path[256];
volatile sig_atomic_t running = 1;

// 前置声明
void broadcast_message(const char *sender, const char *message, int exclude_fd, int msg_type);
int find_client_by_name(const char *name);
int find_client_by_fd(int sockfd);
void remove_client(int index);
void send_user_list_to_client(int client_idx);
void send_system_message_to_client(int client_idx, const char *message);
// 设置socket为非阻塞模式
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl get");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl set");
        return -1;
    }
    return 0;
}

static void log_event(const char *fmt, ...) {
    if (!log_fp) return;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);
    fprintf(log_fp, "[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_fp, fmt, ap);
    va_end(ap);
    fprintf(log_fp, "\n");
    fflush(log_fp);
}

static int init_logger(void) {
    server_start_time = time(NULL);
    struct tm tm_start;
    localtime_r(&server_start_time, &tm_start);
    char tname[32];
    strftime(tname, sizeof(tname), "%Y%m%d_%H%M%S", &tm_start);
    mkdir("logs", 0755);
    snprintf(log_file_path, sizeof(log_file_path), "logs/server_%s.log", tname);
    log_fp = fopen(log_file_path, "a");
    if (!log_fp) return -1;
    char human[64];
    strftime(human, sizeof(human), "%Y-%m-%d %H:%M:%S", &tm_start);
    fprintf(log_fp, "=== Server Start: %s ===\n", human);
    fprintf(log_fp, "Port: %d\n", PORT);
    fflush(log_fp);
    return 0;
}

void close_logger(void) {
    if (log_fp) {
        time_t end = time(NULL);
        struct tm tm_end;
        localtime_r(&end, &tm_end);
        char human[64];
        strftime(human, sizeof(human), "%Y-%m-%d %H:%M:%S", &tm_end);
        fprintf(log_fp, "=== Server Stop: %s ===\n", human);
        fclose(log_fp);
        log_fp = NULL;
    }
}

static void handle_signal(int sig) {
    log_event("Received signal %d, shutting down", sig);
    running = 0;
}

// 处理客户端消息
void handle_client_message(int client_idx) {
    ClientInfo *client = clients[client_idx];
    ChatPacket packet;
    int bytes_read = recv(client->sockfd, &packet, sizeof(ChatPacket), 0);
    
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            printf("✗ %s 连接已断开\n", client->name);
        } else {
            perror("recv");
        }
        log_event("Disconnect %s fd=%d", client->name, client->sockfd);
        
        // 广播用户离线消息
        if (client->logged_in) {
            char leave_msg[BUFFER_SIZE];
            snprintf(leave_msg, BUFFER_SIZE, "[系统] %s 已离线", client->name);
            broadcast_message("系统", leave_msg, -1, MSG_SYSTEM);
        }
        
        remove_client(client_idx);
        return;
    }
    
    MessageType type = packet.type;
    
    switch (type) {
        case MSG_REGISTER: {
            // 客户端注册/登录
            strncpy(client->name, packet.data, NAME_SIZE - 1);
            client->name[NAME_SIZE - 1] = '\0';
            client->logged_in = 1;
            client->login_time = time(NULL);
            
            printf("✓ %s 登录了 (FD: %d)\n", client->name, client->sockfd);
            log_event("Login %s fd=%d", client->name, client->sockfd);
            
            // 发送欢迎消息
            char welcome[BUFFER_SIZE];
            snprintf(welcome, BUFFER_SIZE, "✓ 欢迎 %s 加入聊天室!", client->name);
            send_system_message_to_client(client_idx, welcome);
            
            // 发送当前在线用户列表
            send_user_list_to_client(client_idx);
            
            // 广播有新用户加入
            char join_msg[BUFFER_SIZE];
            snprintf(join_msg, BUFFER_SIZE, "[系统] %s 加入了聊天室", client->name);
            broadcast_message("系统", join_msg, client->sockfd, MSG_SYSTEM);
            log_event("Join %s", client->name);
            
            break;
        }
        
        case MSG_PUBLIC_CHAT: {
            // 公共聊天
            if (client->logged_in) {
                printf("[公开] %s: %s\n", client->name, packet.data);
                broadcast_message(client->name, packet.data, -1, MSG_PUBLIC_CHAT);
                log_event("[public] %s: %s", client->name, packet.data);
            }
            break;
        }
        
        case MSG_PRIVATE_CHAT: {
            // 私聊：格式 "target_name:message"
            if (client->logged_in) {
                char *colon = strchr(packet.data, ':');
                if (colon != NULL) {
                    *colon = '\0';
                    char *target_name = packet.data;
                    char *message = colon + 1;
                    
                    int target_idx = find_client_by_name(target_name);
                    if (target_idx >= 0) {
                        // 发送给目标用户
                        ChatPacket response;
                        char private_msg[BUFFER_SIZE];
                        snprintf(private_msg, BUFFER_SIZE, "[私聊来自 %s]: %s", client->name, message);
                        pack_message(&response, MSG_PRIVATE_CHAT, client->name, private_msg);
                        send(clients[target_idx]->sockfd, &response, sizeof(ChatPacket), 0);
                        
                        printf("[私聊] %s -> %s: %s\n", client->name, target_name, message);
                        log_event("[private] %s -> %s: %s", client->name, target_name, message);
                        
                        // 发送确认给发送者
                        char confirm[BUFFER_SIZE];
                        snprintf(confirm, BUFFER_SIZE, "✓ 私聊已发送给 %s", target_name);
                        send_system_message_to_client(client_idx, confirm);
                    } else {
                        char error_msg[BUFFER_SIZE];
                        snprintf(error_msg, BUFFER_SIZE, "✗ 用户 '%s' 不在线", target_name);
                        send_system_message_to_client(client_idx, error_msg);
                        log_event("[private_error] %s -> %s (not online)", client->name, target_name);
                    }
                }
            }
            break;
        }
        
        case MSG_USER_LIST: {
            // 请求在线用户列表
            if (client->logged_in) {
                send_user_list_to_client(client_idx);
            }
            break;
        }
        
        case MSG_DISCONNECT: {
            // 客户端主动断开连接
            printf("✗ %s 主动断开连接\n", client->name);
            log_event("Disconnect %s fd=%d", client->name, client->sockfd);
            
            char leave_msg[BUFFER_SIZE];
            snprintf(leave_msg, BUFFER_SIZE, "[系统] %s 离开了聊天室", client->name);
            broadcast_message("系统", leave_msg, -1, MSG_SYSTEM);
            
            remove_client(client_idx);
            break;
        }
        
        default:
            printf("未知消息类型: %d\n", type);
            break;
    }
}

// 广播消息给所有连接的客户端
void broadcast_message(const char *sender, const char *message, int exclude_fd, int msg_type) {
    ChatPacket packet;
    pack_message(&packet, msg_type, sender, message);
    
    for (int i = 0; i < client_count; i++) {
        if (clients[i] && clients[i]->logged_in && clients[i]->sockfd != exclude_fd) {
            send(clients[i]->sockfd, &packet, sizeof(ChatPacket), 0);
        }
    }
}

// 发送系统消息给特定客户端
void send_system_message_to_client(int client_idx, const char *message) {
    if (client_idx < 0 || client_idx >= client_count || !clients[client_idx]) {
        return;
    }
    
    ChatPacket packet;
    pack_message(&packet, MSG_SYSTEM, "系统", message);
    send(clients[client_idx]->sockfd, &packet, sizeof(ChatPacket), 0);
}

// 发送在线用户列表给客户端
void send_user_list_to_client(int client_idx) {
    if (client_idx < 0 || client_idx >= client_count || !clients[client_idx]) {
        return;
    }
    
    char list_str[BUFFER_SIZE] = {0};
    int pos = 0;
    
    for (int i = 0; i < client_count; i++) {
        if (clients[i] && clients[i]->logged_in) {
            int len = strlen(clients[i]->name);
            if (pos + len + 3 < BUFFER_SIZE) {
                if (pos > 0) {
                    pos += snprintf(list_str + pos, BUFFER_SIZE - pos, ", ");
                }
                pos += snprintf(list_str + pos, BUFFER_SIZE - pos, "%s", clients[i]->name);
            }
        }
    }
    
    if (pos == 0) {
        snprintf(list_str, BUFFER_SIZE, "(无其他用户在线)");
    }
    
    ChatPacket packet;
    pack_message(&packet, MSG_USER_LIST, "系统", list_str);
    send(clients[client_idx]->sockfd, &packet, sizeof(ChatPacket), 0);
}

// 根据名称查找客户端索引
int find_client_by_name(const char *name) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i] && clients[i]->logged_in && strcmp(clients[i]->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// 根据文件描述符查找客户端索引
int find_client_by_fd(int sockfd) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i] && clients[i]->sockfd == sockfd) {
            return i;
        }
    }
    return -1;
}

// 移除客户端
void remove_client(int index) {
    if (index < 0 || index >= client_count || !clients[index]) {
        return;
    }
    
    close(clients[index]->sockfd);
    free(clients[index]);
    
    // 移动后续元素
    for (int i = index; i < client_count - 1; i++) {
        clients[i] = clients[i + 1];
    }
    
    client_count--;
}
 
// 主函数
int main(void)
{
    struct sockaddr_in addr, client_addr;
    int listen_fd, client_fd;
    int addrlen = sizeof(addr);
    
    // 创建监听socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        perror("create socket");
        return -1;
    }
    
    // 设置socket选项，允许地址立即重用
    int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt");
        close(listen_fd);
        return -1;
    }
    
    // 设置为非阻塞模式
    if (set_nonblocking(listen_fd) < 0)
    {
        close(listen_fd);
        return -1;
    }
    
    // 设置服务器地址
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    // 绑定socket
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(listen_fd);
        return -1;
    }
    
    // 监听
    if (listen(listen_fd, MAX_CLIENTS) < 0)
    {
        perror("listen");
        close(listen_fd);
        return -1;
    }
    
    printf("🔗 服务器启动成功，监听端口 %d (使用 epoll 多路IO)\n", PORT);
    if (init_logger() == 0) {
        log_event("Server started, listening on port %d", PORT);
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // 创建epoll实例
    int epfd = epoll_create1(0);
    if (epfd < 0)
    {
        perror("epoll_create1");
        close(listen_fd);
        return -1;
    }
    
    // 添加监听socket到epoll
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0)
    {
        perror("epoll_ctl add listen");
        close(epfd);
        close(listen_fd);
        return -1;
    }
    
    printf("⏳ 等待客户端连接......\n");
    log_event("Waiting for client connections");
    
    // epoll主循环
    while (running)
    {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds < 0)
        {
            if (errno == EINTR) {
                // 信号打断，继续根据running决定退出
                continue;
            }
            perror("epoll_wait");
            break;
        }
        
        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;
            
            // 处理新的连接请求
            if (fd == listen_fd)
            {
                while (1)
                {
                    memset(&client_addr, 0, sizeof(client_addr));
                    client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
                    
                    if (client_fd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;  // 没有更多连接
                        }
                        else
                        {
                            perror("accept");
                            break;
                        }
                    }
                    
                    // 检查是否超过最大连接数
                    if (client_count >= MAX_CLIENTS)
                    {
                        printf("✗ 连接数已满，拒绝连接\n");
                        close(client_fd);
                        continue;
                    }
                    
                    // 设置客户端socket为非阻塞
                    if (set_nonblocking(client_fd) < 0)
                    {
                        close(client_fd);
                        continue;
                    }
                    
                    // 添加客户端到epoll
                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) < 0)
                    {
                        perror("epoll_ctl add client");
                        close(client_fd);
                        continue;
                    }
                    
                    // 创建客户端结构
                    ClientInfo *client = malloc(sizeof(ClientInfo));
                    if (!client)
                    {
                        perror("malloc");
                        close(client_fd);
                        continue;
                    }
                    
                    memset(client, 0, sizeof(ClientInfo));
                    client->sockfd = client_fd;
                    client->logged_in = 0;
                    clients[client_count] = client;
                    client_count++;
                    
                    printf("📱 新连接接受 (FD: %d, 总连接数: %d)\n", client_fd, client_count);
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                    log_event("New connection fd=%d from %s:%d", client_fd, ip, ntohs(client_addr.sin_port));
                }
            }
            // 处理客户端数据
            else if (events[i].events & EPOLLIN)
            {
                int client_idx = find_client_by_fd(fd);
                if (client_idx >= 0)
                {
                    handle_client_message(client_idx);
                }
            }
            // 处理错误
            else if (events[i].events & (EPOLLERR | EPOLLHUP))
            {
                int client_idx = find_client_by_fd(fd);
                if (client_idx >= 0)
                {
                    printf("✗ 连接错误: %s (FD: %d)\n", clients[client_idx]->name, fd);
                    log_event("Connection error %s fd=%d", clients[client_idx]->name, fd);
                    remove_client(client_idx);
                }
                else
                {
                    close(fd);
                }
            }
        }
    }
    
    // 清理
    close(epfd);
    close(listen_fd);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i])
        {
            close(clients[i]->sockfd);
            free(clients[i]);
        }
    }
    
    close_logger();
    return 0;
}
