#pragma once
#include <Arduino.h>

// =============================================================
// Message Types (SINGLE SOURCE OF TRUTH)
// =============================================================
#define MSG_BUTTON_SHORT   1
#define MSG_BUTTON_LONG    2
#define MSG_STEP_TRIGGER   3
#define MSG_NOTE_EVENT    10

// =============================================================
// Message Structure
// =============================================================
struct Message {
    uint8_t type;
    int32_t data1;
    int32_t data2;
};

// =============================================================
// Message Bus
// =============================================================
class MessageBus {
public:
    static const int MAX_MESSAGES = 16;
    Message queue[MAX_MESSAGES];
    volatile int head = 0;
    volatile int tail = 0;

    bool send(const Message& m) {
        int next = (head + 1) % MAX_MESSAGES;
        if (next == tail) return false;   // queue full
        queue[head] = m;
        head = next;
        return true;
    }

    bool receive(Message& m) {
        if (tail == head) return false;   // queue empty
        m = queue[tail];
        tail = (tail + 1) % MAX_MESSAGES;
        return true;
    }
};
