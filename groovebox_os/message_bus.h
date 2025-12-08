#pragma once
#include <Arduino.h>

struct Message {
    uint8_t type;
    int32_t data1;
    int32_t data2;
};

class MessageBus {
public:
    static const int MAX_MESSAGES = 16;
    Message queue[MAX_MESSAGES];
    volatile int head = 0;
    volatile int tail = 0;

    bool send(const Message &msg) {
        int next = (head + 1) % MAX_MESSAGES;
        if (next == tail) return false; // queue full
        queue[head] = msg;
        head = next;
        return true;
    }

    bool receive(Message &msg) {
        if (tail == head) return false; // empty
        msg = queue[tail];
        tail = (tail + 1) % MAX_MESSAGES;
        return true;
    }
};

extern MessageBus BUS;
