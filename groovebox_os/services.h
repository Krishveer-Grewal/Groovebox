#pragma once
#include <Arduino.h>
#include "service_base.h"
#include "message_bus.h"

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
            m.data1 = 0;
            m.data2 = 0;
            BUS.send(m);
        }
        last = cur;
    }
};

// =============================================================
// UI SERVICE
// =============================================================
class UIService : public Service {
public:
    SystemState state = STATE_IDLE;
    int cursor = 0;
    bool pattern[4] = {false, false, false, false};

    float bpm = 120.0f;
    unsigned long stepIntervalMs = 500;
    unsigned long lastTapTime = 0;

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
            }
        }
        lastTapTime = now;
    }

    void update() override {
        Message msg;
        while (BUS.receive(msg)) {
            switch (msg.type) {
                case MSG_BUTTON_SHORT:
                    if (state == STATE_RUNNING) handleTapTempo();
                    else if (state == STATE_IDLE) state = STATE_RUNNING;
                    else if (state == STATE_EDIT) {
                        unsigned long now = millis();
                        if (now - lastEditShort < doubleTapWindow) {
                            state = STATE_RUNNING;
                            lastEditShort = 0;
                        } else {
                            cursor = (cursor + 1) % 4;
                            lastEditShort = now;
                        }
                    }
                    break;

                case MSG_BUTTON_LONG:
                    if (state != STATE_EDIT) {
                        state = STATE_EDIT;
                        cursor = 0;
                    } else {
                        pattern[cursor] = !pattern[cursor];
                    }
                    break;
            }
        }
    }
};

// =============================================================
// AUDIO SERVICE
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
// LED GRID SERVICE
// =============================================================
class LEDGridService : public Service {
    int pins[4];
    SystemState* state;
    int* cursor;
    bool* pattern;

public:
    int currentStep = 0;

    LEDGridService(int p0, int p1, int p2, int p3,
                   SystemState* s, int* c, bool* pat)
        : state(s), cursor(c), pattern(pat)
    {
        pins[0]=p0; pins[1]=p1; pins[2]=p2; pins[3]=p3;
    }

    void init() override {
        for (int i=0;i<4;i++) {
            pinMode(pins[i], OUTPUT);
            digitalWrite(pins[i], LOW);
        }
    }

    void update() override {
        for (int i=0;i<4;i++) {
            digitalWrite(pins[i],
                (*state == STATE_RUNNING && i == currentStep) ||
                (*state == STATE_EDIT && i == *cursor) ||
                (pattern[i])
            );
        }
    }
};

// =============================================================
// CLOUD SERVICE (MQTT)
// =============================================================
#include <WiFi.h>
#include <PubSubClient.h>

class CloudService : public Service {
    const char* ssid;
    const char* password;
    const char* broker;

    WiFiClient wifiClient;
    PubSubClient mqtt;

    UIService* ui;

public:
    CloudService(const char* s, const char* p, const char* b, UIService* u)
        : ssid(s), password(p), broker(b), mqtt(wifiClient), ui(u) {}

    void init() override {
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) delay(500);
        mqtt.setServer(broker, 1883);
        mqtt.connect("groovebox-esp32");
    }

    void update() override {
        mqtt.loop();

        static float lastBpm = -1;
        static SystemState lastState = STATE_IDLE;

        if (ui->bpm != lastBpm) {
            char buf[64];
            snprintf(buf,sizeof(buf),"{\"bpm\":%.2f}",ui->bpm);
            mqtt.publish("groovebox/tempo",buf);
            lastBpm = ui->bpm;
        }

        if (ui->state != lastState) {
            mqtt.publish("groovebox/transport",
                ui->state==STATE_RUNNING ? "start":"stop");
            lastState = ui->state;
        }
    }

    void publishNote(uint8_t note, uint8_t velocity) {
        char buf[64];
        snprintf(buf,sizeof(buf),
            "{\"note\":%d,\"velocity\":%d}",note,velocity);
        mqtt.publish("groovebox/note",buf);
    }
};

// =============================================================
// SEQUENCER SERVICE
// =============================================================
class SequencerService : public Service {
    unsigned long last = 0;
    int step = 0;

    UIService* ui;
    LEDGridService* leds;
    AudioService* audio;
    CloudService* cloud;

public:
    SequencerService(UIService* u, LEDGridService* l,
                     AudioService* a, CloudService* c)
        : ui(u), leds(l), audio(a), cloud(c) {}

    void update() override {
        if (ui->state != STATE_RUNNING) return;

        unsigned long now = millis();
        if (now - last >= ui->getStepInterval()) {
            last = now;
            leds->currentStep = step;

            if (ui->pattern[step]) {
                audio->playClick();
                cloud->publishNote(36,100);
            }

            step = (step + 1) % 4;
        }
    }
};
// =============================================================
// LOG SERVICE (NO-OP)
// =============================================================
class LogService : public Service {
public:
    void init() override {}
    void update() override {}
};
