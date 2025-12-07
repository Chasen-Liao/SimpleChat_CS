#include "../include/protocol.h"

// 打包消息函数
void pack_message(ChatPacket *packet, MessageType type, const char *sender, const char *data) {
    if (packet == NULL) {
        return;
    }
    
    packet->type = type;
    
    if (sender != NULL) {
        strncpy(packet->sender, sender, NAME_SIZE - 1);
        packet->sender[NAME_SIZE - 1] = '\0';
    } else {
        packet->sender[0] = '\0';
    }
    
    if (data != NULL) {
        strncpy(packet->data, data, BUFFER_SIZE - 1);
        packet->data[BUFFER_SIZE - 1] = '\0';
        packet->length = strlen(packet->data);
    } else {
        packet->data[0] = '\0';
        packet->length = 0;
    }
}

// 解包消息函数
void unpack_message(ChatPacket *packet, MessageType *type, char *sender, char *data) {
    if (packet == NULL || type == NULL || sender == NULL || data == NULL) {
        return;
    }
    
    *type = packet->type;
    strncpy(sender, packet->sender, NAME_SIZE - 1);
    sender[NAME_SIZE - 1] = '\0';
    strncpy(data, packet->data, BUFFER_SIZE - 1);
    data[BUFFER_SIZE - 1] = '\0';
}
