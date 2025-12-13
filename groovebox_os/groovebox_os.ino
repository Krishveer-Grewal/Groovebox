#include <Arduino.h>
#include "service_base.h"
#include "message_bus.h"

#include <WiFi.h>
#include <PubSubClient.h>

// ------------------------------
// CONFIG
// ------------------------------
#define NUM_STEPS 8
#define VISIBLE_STEPS 4

// WiFi + MQTT (EDIT THESE)
static const char* WIFI_SSID     = "YOUR_WIFI_NAME";
static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// MQTT broker = your Mosquitto machine IP (example: your Linux laptop running mosquitto)
static const char* MQTT_BROKER_IP = "10.0.0.126";
static const int   MQTT_BROKER_PORT = 1883;

// ------------------------------
// Message Types (for Button -> UI)
// ------------------------------
#define MSG_BUTTON_SHORT  1
#define MSG_BUTTON_LONG   2

// ------------------------------
// System State
// ------------------------------
enum SystemState {
  STATE_IDLE,
  STATE_RUNNING,
  STATE_EDIT
};

// ------------------------------
// Globals
// ------------------------------
MessageBus BUS;

// =============================================================
// Cloud (MQTT) Publisher Service
// =============================================================
class CloudService : public Service {
  WiFiClient wifiClient;
  PubSubClient mqtt;

  unsigned long lastReconnectAttempt = 0;

public:
  CloudService() : mqtt(wifiClient) {}

  void init() override {
    Serial.println("[CLOUD] init()");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("[CLOUD] Connecting to WiFi");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
      delay(300);
      Serial.print(".");
      // Don't hard-lock forever
      if (millis() - start > 15000) {
        Serial.println("\n[CLOUD] WiFi connect timeout (continuing without MQTT)");
        return;
      }
    }
    Serial.println("\n[CLOUD] WiFi connected");
    Serial.print("[CLOUD] IP: ");
    Serial.println(WiFi.localIP());

    mqtt.setServer(MQTT_BROKER_IP, MQTT_BROKER_PORT);
    reconnect();
  }

  void reconnect() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqtt.connected()) return;

    Serial.print("[CLOUD] MQTT connecting… ");
    // clientId must be unique-ish
    String clientId = "groovebox-esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed rc=");
      Serial.println(mqtt.state());
    }
  }

  void loopMqtt() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!mqtt.connected()) {
      unsigned long now = millis();
      if (now - lastReconnectAttempt > 1000) {
        lastReconnectAttempt = now;
        reconnect();
      }
      return;
    }
    mqtt.loop();
  }

  bool isConnected() const {
    return (WiFi.status() == WL_CONNECTED) && mqtt.connected();
  }

  // --- Publish helpers ---
  void publishTransport(bool running) {
    if (!isConnected()) return;
    mqtt.publish("groovebox/transport", running ? "start" : "stop");
    Serial.print("[CLOUD] transport -> ");
    Serial.println(running ? "start" : "stop");
  }

  void publishTempo(float bpm) {
    if (!isConnected()) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "{ \"bpm\": %.2f }", bpm);
    mqtt.publish("groovebox/tempo", buf);
    Serial.print("[CLOUD] tempo -> ");
    Serial.println(buf);
  }

  void publishNote(int note, int velocity, bool on, int step) {
    if (!isConnected()) return;
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{ \"note\": %d, \"velocity\": %d, \"on\": %s, \"step\": %d }",
             note, velocity, on ? "true" : "false", step);
    mqtt.publish("groovebox/note", buf);
    Serial.print("[CLOUD] note -> ");
    Serial.println(buf);
  }

  void update() override {
    loopMqtt();
  }
};

CloudService cloud;

// =============================================================
// Button Service
// =============================================================
class ButtonService : public Service {
  int pin;
  bool last = HIGH;
  unsigned long pressTime = 0;

public:
  ButtonService(int p) : pin(p) {}

  void init() override {
    pinMode(pin, INPUT_PULLUP);
    Serial.println("[Button] init()");
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
// UI Service (state + cursor + pattern + tap tempo)
// =============================================================
class UIService : public Service {
public:
  SystemState state = STATE_IDLE;
  bool pattern[NUM_STEPS] = { false };
  int cursor = 0;

  // tempo
  float bpm = 120.0f;
  unsigned long stepIntervalMs = 500;
  unsigned long lastTapTime = 0;

  // edit UX
  unsigned long lastEditShort = 0;
  const unsigned long doubleTapWindow = 400;

  // publish guards
  SystemState lastPublishedState = STATE_IDLE;
  float lastPublishedBpm = -1.0f;

  void init() override {
    Serial.println("[UI] init()");
  }

  int page() const {
    return cursor / VISIBLE_STEPS;
  }

  unsigned long getStepInterval() const {
    return stepIntervalMs;
  }

  void handleTapTempo() {
    if (state != STATE_RUNNING) return;

    unsigned long now = millis();
    if (lastTapTime != 0) {
      unsigned long delta = now - lastTapTime;

      if (delta >= 150 && delta <= 2000) {
        float newBpm = 60000.0f / (float)delta;
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
    // 1) handle button messages
    Message msg;
    while (BUS.receive(msg)) {
      switch (msg.type) {

        case MSG_BUTTON_SHORT:
          if (state == STATE_IDLE) {
            state = STATE_RUNNING;
            Serial.println("[UI] State -> RUNNING");
          }
          else if (state == STATE_RUNNING) {
            handleTapTempo();
          }
          else if (state == STATE_EDIT) {
            unsigned long now = millis();
            if (now - lastEditShort < doubleTapWindow) {
              state = STATE_RUNNING;
              Serial.println("[UI] EXIT EDIT -> RUNNING");
              lastEditShort = 0;
            } else {
              cursor = (cursor + 1) % NUM_STEPS;
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

        default:
          break;
      }
    }

    // 2) publish state/tempo changes (non-blocking)
    if (state != lastPublishedState) {
      cloud.publishTransport(state == STATE_RUNNING);
      lastPublishedState = state;
    }

    // publish bpm changes (only when running OR after a tap)
    if (fabs(bpm - lastPublishedBpm) > 0.01f) {
      cloud.publishTempo(bpm);
      lastPublishedBpm = bpm;
    }
  }
};

UIService ui;

// =============================================================
// LED Grid Service
// =============================================================
class LEDGridService : public Service {
  int pins[VISIBLE_STEPS];
  UIService* uiRef;

  unsigned long lastBlink = 0;
  bool blink = false;

public:
  int currentStep = -1;

  LEDGridService(int p0, int p1, int p2, int p3, UIService* u)
    : uiRef(u)
  {
    pins[0] = p0;
    pins[1] = p1;
    pins[2] = p2;
    pins[3] = p3;
  }

  void init() override {
    for (int i = 0; i < VISIBLE_STEPS; i++) {
      pinMode(pins[i], OUTPUT);
      digitalWrite(pins[i], LOW);
    }
    Serial.println("[LED] init()");
  }

  void update() override {
    unsigned long now = millis();
    if (now - lastBlink > 150) {
      blink = !blink;
      lastBlink = now;
    }

    int baseStep = uiRef->page() * VISIBLE_STEPS;

    for (int i = 0; i < VISIBLE_STEPS; i++) {
      int stepIndex = baseStep + i;
      bool on = false;

      if (uiRef->state == STATE_RUNNING && stepIndex == currentStep) {
        on = true;  // playhead
      }
      else if (uiRef->state == STATE_EDIT && stepIndex == uiRef->cursor) {
        on = true;  // cursor highlight
      }
      else if (stepIndex < NUM_STEPS && uiRef->pattern[stepIndex]) {
        on = blink; // pattern steps blink
      }

      digitalWrite(pins[i], on ? HIGH : LOW);
    }
  }
};

LEDGridService leds(17, 4, 5, 18, &ui);

// =============================================================
// Sequencer Service
// =============================================================
class SequencerService : public Service {
  UIService* uiRef;
  LEDGridService* ledsRef;

  unsigned long lastStepTime = 0;
  int step = 0;

public:
  SequencerService(UIService* u, LEDGridService* l)
    : uiRef(u), ledsRef(l) {}

  void init() override {
    Serial.println("[SEQ] init()");
  }

  void update() override {
    if (uiRef->state != STATE_RUNNING) return;

    unsigned long now = millis();
    unsigned long interval = uiRef->getStepInterval();
    if (interval < 50) interval = 50;

    if (now - lastStepTime < interval) return;
    lastStepTime = now;

    ledsRef->currentStep = step;

    // Trigger: publish note event when step is ON
    if (uiRef->pattern[step]) {
      Serial.print("[SEQ] Step ");
      Serial.print(step);
      Serial.println(" TRIGGER");

      // Kick note = 36 (C2) is a good default for FL/FPC
      cloud.publishNote(36, 100, true, step);
      // optional: send note off shortly after (bridge can also gate)
      cloud.publishNote(36, 0, false, step);
    }

    step = (step + 1) % NUM_STEPS;
  }
};

SequencerService sequencer(&ui, &leds);

// =============================================================
// Services
// =============================================================
ButtonService button(16);

Service* services[] = {
  &cloud,
  &button,
  &ui,
  &leds,
  &sequencer
};

const int NUM_SERVICES = sizeof(services) / sizeof(Service*);

// =============================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("=== BOOT ===");

  for (int i = 0; i < NUM_SERVICES; i++) {
    services[i]->init();
  }

  Serial.println("=== READY ===");
}

void loop() {
  for (int i = 0; i < NUM_SERVICES; i++) {
    services[i]->update();
  }
}
