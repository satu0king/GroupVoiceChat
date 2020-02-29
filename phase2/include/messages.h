#pragma once

#define CHAT_MESSAGE_SIZE 200
#define VOICE_MESSAGE_SIZE 1024

struct JoinRequest {
    char groupName[20];
    char name[20];
};

struct JoinResponse {
    int id;
    int groupId;
};

enum MessageType { VOICE, CHAT };

struct ChatMessage {
    int id;
    int groupId;
    struct timeval time;
    char name[20];
    char message[CHAT_MESSAGE_SIZE];
};

struct VoiceMessage {
    int id;
    int groupId;
    char message[VOICE_MESSAGE_SIZE];
};

enum Mode { DEV, TEST, PROD };
