#pragma once
#include "service_base.h"
#include "message_bus.h"

// Services = Mini-Processes

// Message types
enum {
    MSG_BUTTON_PRESS = 1,
    MSG_STEP_TRIGGER = 2
};

// -------------- LED SERVICE ----------------
class LEDService : public Service {
    int pin;
    unsigned long lastBlink = 0;
    bool state = false;

public:
    LEDService(int p) : pin(p) {}

    void init() override {
        pinMode(pin, OUTPUT);
    }

    void update() override {
        unsigned long now = millis();
        if (now - lastBlink >= 500) {
            lastBlink = now;
            state = !state;
            digitalWrite(pin, state);
        }
    }
};

// -------------- BUTTON SERVICE ----------------
class ButtonService : public Service {
    int pin;
    bool lastState = HIGH;

public:
    ButtonService(int p) : pin(p) {}

    void init() override {
        pinMode(pin, INPUT_PULLUP);
    }

    void update() override {
        bool current = digitalRead(pin);
        if (lastState == HIGH && current == LOW) {
            Message m;
            m.type = MSG_BUTTON_PRESS;
            m.data1 = 1;
            BUS.send(m);
        }
        lastState = current;
    }
};

// -------------- SEQUENCER SERVICE ----------------
class SequencerService : public Service {
    int step = 0;
    unsigned long lastStep = 0;

public:
    void update() override {
        unsigned long now = millis();
        if (now - lastStep >= 250) { // 4 Hz → 16 steps per second-ish
            lastStep = now;

            Message m;
            m.type = MSG_STEP_TRIGGER;
            m.data1 = step;
            BUS.send(m);

            step = (step + 1) % 16;
        }
    }
};

// -------------- LOGGING SERVICE ----------------
class LogService : public Service {
public:
    void update() override {
        Message msg;
        while (BUS.receive(msg)) {
            switch (msg.type) {
                case MSG_BUTTON_PRESS:
                    Serial.println("[LOG] Button Pressed!");
                    break;

                case MSG_STEP_TRIGGER:
                    Serial.print("[LOG] Step: ");
                    Serial.println(msg.data1);
                    break;
            }
        }
    }
};
