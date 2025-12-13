#pragma once
#include <Arduino.h>

struct Message {
    uint8_t type;
    int32_t data1;
    int32_t data2;
    int32_t data3;
};

class MessageBus {
public:
    static const int MAX_MESSAGES = 32;
    Message queue[MAX_MESSAGES];
    volatile int head = 0;
    volatile int tail = 0;

    bool send(const Message& m) {
        int next = (head + 1) % MAX_MESSAGES;
        if (next == tail) return false; // full
        queue[head] = m;
        head = next;
        return true;
    }

    bool receive(Message& m) {
        if (tail == head) return false; // empty
        m = queue[tail];
        tail = (tail + 1) % MAX_MESSAGES;
        return true;
    }
};
