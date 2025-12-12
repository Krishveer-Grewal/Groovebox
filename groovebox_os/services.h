#pragma once
#include <Arduino.h>
#include "service_base.h"
#include "message_bus.h"

// -------------------------------------------------------------
// Messages
// -------------------------------------------------------------
enum {
    MSG_BUTTON_SHORT = 1,
    MSG_BUTTON_LONG  = 2,
    MSG_STEP_TRIGGER = 3
};

// -------------------------------------------------------------
// System State
// -------------------------------------------------------------
enum SystemState {
    STATE_IDLE,
    STATE_RUNNING,
    STATE_EDIT
};

extern MessageBus BUS;

// =============================================================
// BUTTON SERVICE
// =============================================================
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
        bool cur = digitalRead(pin);

        if (last == HIGH && cur == LOW) {
            pressTime = millis();
        }
        if (last == LOW && cur == HIGH) {
            unsigned long d = millis() - pressTime;

            Message m;
            m.type = (d < 500) ? MSG_BUTTON_SHORT : MSG_BUTTON_LONG;
            BUS.send(m);
        }
        last = cur;
    }
};

// =============================================================
// UI SERVICE (STATE + PATTERN + TAP TEMPO)
// =============================================================
class UIService : public Service {
public:
    SystemState state = STATE_IDLE;
    int cursor = 0;
    bool pattern[4] = {};

    // tempo
    float bpm = 120.0f;
    unsigned long stepIntervalMs = 500;
    unsigned long lastTapTime = 0;

    // edit UX
    unsigned long lastEditShort = 0;
    const unsigned long doubleTapWindow = 400;

    unsigned long getStepInterval() const {
        return stepIntervalMs;
    }

    void handleTapTempo() {
        if (state != STATE_RUNNING) return;

        unsigned long now = millis();
        if (lastTapTime != 0) {
            unsigned long delta = now - lastTapTime;

            if (delta >= 150 && delta <= 2000) {
                float newBpm = 60000.0f / delta;
                bpm = bpm * 0.6f + newBpm * 0.4f;
                bpm = constrain(bpm, 40.0f, 220.0f);
                stepIntervalMs = (unsigned long)(60000.0f / bpm);

                Serial.print("[CLK] Tap tempo -> ");
                Serial.print(bpm);
                Serial.println(" BPM");
            }
        }
        lastTapTime = now;
    }

    void update() override {
        Message msg;
        while (BUS.receive(msg)) {
            switch (msg.type) {

                case MSG_BUTTON_SHORT:
                    if (state == STATE_RUNNING) {
                        handleTapTempo();
                    }
                    else if (state == STATE_IDLE) {
                        state = STATE_RUNNING;
                        Serial.println("[UI] State -> RUNNING");
                    }
                    else if (state == STATE_EDIT) {
                        unsigned long now = millis();
                        if (now - lastEditShort < doubleTapWindow) {
                            state = STATE_RUNNING;
                            Serial.println("[UI] EXIT EDIT -> RUNNING");
                            lastEditShort = 0;
                        } else {
                            cursor = (cursor + 1) % 4;
                            Serial.print("[UI] Cursor -> ");
                            Serial.println(cursor);
                            lastEditShort = now;
                        }
                    }
                    break;

                case MSG_BUTTON_LONG:
                    if (state != STATE_EDIT) {
                        state = STATE_EDIT;
                        cursor = 0;
                        Serial.println("[UI] State -> EDIT");
                    } else {
                        pattern[cursor] = !pattern[cursor];
                        Serial.print("[UI] Step ");
                        Serial.print(cursor);
                        Serial.print(" -> ");
                        Serial.println(pattern[cursor] ? "ON" : "OFF");
                    }
                    break;
            }
        }
    }
};

// =============================================================
// AUDIO SERVICE (LOGIC ONLY FOR NOW)
// =============================================================
class AudioService : public Service {
    int pin;
    int channel;
    bool active = false;
    unsigned long offAt = 0;

public:
    AudioService(int p, int ch = 0) : pin(p), channel(ch) {}

    void init() override {
        ledcSetup(channel, 4000, 8);
        ledcAttachPin(pin, channel);
        ledcWrite(channel, 0);
    }

    void playClick() {
        active = true;
        offAt = millis() + 40;
        ledcWrite(channel, 60);
    }

    void update() override {
        if (active && millis() >= offAt) {
            ledcWrite(channel, 0);
            active = false;
        }
    }
};

// =============================================================
// LED GRID SERVICE (SMOOTH + BEAT FLASH)
// =============================================================
class LEDGridService : public Service {
    int pins[4];
    SystemState* state;
    int* cursor;
    bool* pattern;

    unsigned long lastBlink = 0;
    bool blinkState = false;

public:
    int currentStep = 0;

    // beat flash
    bool beatFlash = false;
    unsigned long beatFlashUntil = 0;

    LEDGridService(int p0, int p1, int p2, int p3,
                   SystemState* s, int* c, bool* pat)
    {
        pins[0] = p0; pins[1] = p1; pins[2] = p2; pins[3] = p3;
        state = s; cursor = c; pattern = pat;
    }

    void init() override {
        for (int i = 0; i < 4; i++) {
            pinMode(pins[i], OUTPUT);
            digitalWrite(pins[i], LOW);
        }
    }

    void update() override {
        unsigned long now = millis();

        // beat flash overrides everything
        if (beatFlash) {
            if (now < beatFlashUntil) {
                for (int i = 0; i < 4; i++) digitalWrite(pins[i], HIGH);
                return;
            } else {
                beatFlash = false;
            }
        }

        // blink timing (desynced from sequencer)
        if (now - lastBlink > 150) {
            blinkState = !blinkState;
            lastBlink = now;
        }

        switch (*state) {

            case STATE_RUNNING:
                for (int i = 0; i < 4; i++) {
                    if (i == currentStep) {
                        digitalWrite(pins[i], HIGH);
                    } else if (pattern[i]) {
                        digitalWrite(pins[i], blinkState ? HIGH : LOW);
                    } else {
                        digitalWrite(pins[i], LOW);
                    }
                }
                break;

            case STATE_EDIT:
                for (int i = 0; i < 4; i++) {
                    if (i == *cursor) {
                        digitalWrite(pins[i], HIGH);
                    } else if (pattern[i]) {
                        digitalWrite(pins[i], blinkState ? HIGH : LOW);
                    } else {
                        digitalWrite(pins[i], LOW);
                    }
                }
                break;

            default:
                for (int i = 0; i < 4; i++) digitalWrite(pins[i], LOW);
                break;
        }
    }
};

// =============================================================
// SEQUENCER SERVICE (CLOCK-DRIVEN)
// =============================================================
class SequencerService : public Service {
    unsigned long last = 0;
    int step = 0;
    UIService* ui;
    LEDGridService* leds;
    AudioService* audio;

public:
    SequencerService(UIService* u, LEDGridService* l, AudioService* a)
        : ui(u), leds(l), audio(a) {}

    void update() override {
        if (ui->state != STATE_RUNNING) return;

        unsigned long now = millis();
        unsigned long interval = ui->getStepInterval();
        if (interval < 50) interval = 50;

        if (now - last >= interval) {
            last = now;

            leds->currentStep = step;

            if (step == 0) {
                leds->beatFlash = true;
                leds->beatFlashUntil = millis() + 60;
            }

            if (ui->pattern[step]) {
                audio->playClick();
            }

            Message m;
            m.type = MSG_STEP_TRIGGER;
            m.data1 = step;
            BUS.send(m);

            step = (step + 1) % 4;
        }
    }
};

// =============================================================
// LOG SERVICE (OPTIONAL)
// =============================================================
class LogService : public Service {
public:
    void update() override {}
};
