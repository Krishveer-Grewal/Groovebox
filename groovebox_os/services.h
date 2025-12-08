#pragma once
#include "service_base.h"
#include "message_bus.h"

// ------------------------- Message Types --------------------------
enum {
    MSG_BUTTON_PRESS = 1,
    MSG_STEP_TRIGGER = 2,
    MSG_BUTTON_SHORT = 3,
    MSG_BUTTON_LONG = 4,
    MSG_STATE_CHANGE = 5
};

// ---------------------- System State Enum -------------------------
enum SystemState {
    STATE_IDLE,
    STATE_RUNNING,
    STATE_EDIT
};

// ------------------------- LED SERVICE ----------------------------
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

// ------------------------- BUTTON SERVICE -------------------------
class ButtonService : public Service {
    int pin;
    bool last = HIGH;
    unsigned long pressTime = 0;

public:
    ButtonService(int p) : pin(p) {}

    void init() override {
        pinMode(pin, INPUT_PULLUP);
    }

    void update() override {
        bool current = digitalRead(pin);

        if (last == HIGH && current == LOW) {
            pressTime = millis();
        }

        if (last == LOW && current == HIGH) {
            unsigned long duration = millis() - pressTime;

            Message m;
            if (duration < 500) {
                m.type = MSG_BUTTON_SHORT;
            } else {
                m.type = MSG_BUTTON_LONG;
            }
            BUS.send(m);
        }

        last = current;
    }
};

// ------------------------- UI SERVICE -------------------------
class UIService : public Service {
public:
    SystemState state = STATE_IDLE;

    void update() override {
        Message msg;
        while (BUS.receive(msg)) {
            switch (msg.type) {

                case MSG_BUTTON_SHORT:
                    if (state == STATE_IDLE || state == STATE_EDIT) {
                        state = STATE_RUNNING;
                        Serial.println("[UI] State → RUNNING");
                    }
                    else if (state == STATE_RUNNING) {
                        state = STATE_IDLE;
                        Serial.println("[UI] State → IDLE");
                    }
                    break;

                case MSG_BUTTON_LONG:
                    state = STATE_EDIT;
                    Serial.println("[UI] State → EDIT");
                    break;

                case MSG_STEP_TRIGGER:
                    if (state == STATE_RUNNING) {
                        Serial.print("[SEQ] Step: ");
                        Serial.println(msg.data1);
                    }
                    break;
            }
        }
    }
};

// ----------------------- SEQUENCER SERVICE ------------------------
class SequencerService : public Service {
    int step = 0;
    unsigned long lastStep = 0;
    UIService* ui;

public:
    SequencerService(UIService* uiService) : ui(uiService) {}

    void update() override {
        if (ui->state != STATE_RUNNING) return;

        unsigned long now = millis();
        if (now - lastStep >= 250) {
            lastStep = now;

            Message m;
            m.type = MSG_STEP_TRIGGER;
            m.data1 = step;
            BUS.send(m);

            step = (step + 1) % 16;
        }
    }
};

// --------------------------- LOG SERVICE ---------------------------
class LogService : public Service {
public:
    void update() override {
        // Intentionally left empty for phase 2, UIService handles logs
    }
};
