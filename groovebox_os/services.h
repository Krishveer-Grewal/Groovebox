#pragma once
#include "service_base.h"
#include "message_bus.h"

// ------------------------- Message Types --------------------------
enum {
    MSG_BUTTON_SHORT = 3,
    MSG_BUTTON_LONG = 4,
    MSG_STEP_TRIGGER = 2,
    MSG_STATE_CHANGE = 5,
    MSG_CURSOR_MOVE = 6,
    MSG_TOGGLE_STEP = 7
};

// ---------------------- System State Enum -------------------------
enum SystemState {
    STATE_IDLE,
    STATE_RUNNING,
    STATE_EDIT
};

// =================================================================
// LED GRID SERVICE (
// =================================================================
class LEDGridService : public Service {
public:
    int pins[4];       // 4 LEDs for steps 0-3
    bool states[4];    // ON/OFF per LED
    int currentStep = -1;
    SystemState* statePtr;
    int* cursorPtr;
    bool* patternPtr;

    LEDGridService(int p0, int p1, int p2, int p3,
                   SystemState* s, int* c, bool* pat)
    {
        pins[0] = p0; pins[1] = p1; pins[2] = p2; pins[3] = p3;
        statePtr = s;
        cursorPtr = c;
        patternPtr = pat;
    }

    void init() override {
        for (int i = 0; i < 4; i++) {
            pinMode(pins[i], OUTPUT);
            digitalWrite(pins[i], LOW);
        }
    }

    void update() override {
        // Clear LEDs
        for (int i = 0; i < 4; i++)
            digitalWrite(pins[i], LOW);

        if (*statePtr == STATE_RUNNING) {
            digitalWrite(pins[currentStep], HIGH);
        }

        if (*statePtr == STATE_EDIT) {
            // Cursor position LED ON
            digitalWrite(pins[*cursorPtr], HIGH);
        }
    }
    
};

// =================================================================
// BUTTON SERVICE
// =================================================================
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

        if (last == HIGH && current == LOW)
            pressTime = millis();

        if (last == LOW && current == HIGH) {
            unsigned long d = millis() - pressTime;
            Message m;
            m.type = (d < 500) ? MSG_BUTTON_SHORT : MSG_BUTTON_LONG;
            BUS.send(m);
        }

        last = current;
    }
};

// =================================================================
// UI SERVICE
// =================================================================
class UIService : public Service {
public:
    SystemState state = STATE_IDLE;
    int cursor = 0;
    bool pattern[16] = {};

    unsigned long lastEditShortPress = 0;   // for double-tap detection
    const unsigned long doubleTapWindow = 400; // ms

    void update() override {
        Message msg;
        while (BUS.receive(msg)) {

            switch (msg.type) {

                case MSG_BUTTON_SHORT:
                    // --------------------------------------
                    // IDLE → RUNNING
                    // RUNNING → IDLE
                    // --------------------------------------
                    if (state == STATE_IDLE) {
                        state = STATE_RUNNING;
                        Serial.println("[UI] State -> RUNNING");
                    }
                    else if (state == STATE_RUNNING) {
                        state = STATE_IDLE;
                        Serial.println("[UI] State -> IDLE");
                    }
                    else if (state == STATE_EDIT) {
                        // --------------------------------------
                        //   EDIT MODE SHORT PRESS BEHAVIOR
                        // --------------------------------------

                        unsigned long now = millis();
                        if (now - lastEditShortPress < doubleTapWindow) {
                            // DOUBLE TAP → EXIT EDIT
                            state = STATE_RUNNING;
                            Serial.println("[UI] EXIT EDIT (double tap) -> RUNNING");
                            lastEditShortPress = 0; // reset
                        }
                        else {
                            // SINGLE TAP → MOVE CURSOR
                            cursor = (cursor + 1) % 4;
                            Serial.print("[UI] Cursor -> ");
                            Serial.println(cursor);
                            lastEditShortPress = now;
                        }
                    }
                    break;

                case MSG_BUTTON_LONG:
                    // --------------------------------------
                    // LONG PRESS: enter EDIT or toggle step
                    // --------------------------------------
                    if (state != STATE_EDIT) {
                        state = STATE_EDIT;
                        cursor = 0;  // optional: reset cursor on entering EDIT
                        Serial.println("[UI] State -> EDIT");
                    } else {
                        // TOGGLE PATTERN STEP
                        pattern[cursor] = !pattern[cursor];
                        Serial.print("[UI] Toggled step ");
                        Serial.print(cursor);
                        Serial.print(" -> ");
                        Serial.println(pattern[cursor] ? "ON" : "OFF");
                    }
                    break;

                case MSG_STEP_TRIGGER:
                    // handled by sequencer / LED services
                    break;
            }
        }
    }
};



// =================================================================
// SEQUENCER SERVICE — AWARE OF UI STATE
// =================================================================
class SequencerService : public Service {
    unsigned long last = 0;
    int step = 0;
    UIService* ui;
    LEDGridService* grid;

public:
    SequencerService(UIService* u, LEDGridService* g)
        : ui(u), grid(g) {}

    void update() override {
        if (ui->state != STATE_RUNNING) return;

        unsigned long now = millis();
        if (now - last >= 250) {
            last = now;

            grid->currentStep = step;

            Message m;
            m.type = MSG_STEP_TRIGGER;
            m.data1 = step;
            BUS.send(m);

            step = (step + 1) % 4; // only 4 LEDs for now
        }
    }
};

// =================================================================
// LOG SERVICE 
// =================================================================
class LogService : public Service {
public:
    void update() override {}
};
