#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "service_base.h"
#include "message_bus.h"

// -----------------------------
// CONFIG
// -----------------------------
#define NUM_STEPS 8
#define VISIBLE_STEPS 4

// -----------------------------
// Message Types (local bus only)
// -----------------------------
enum {
  MSG_BUTTON_SHORT = 1,
  MSG_BUTTON_LONG  = 2,
};

// -----------------------------
// System State
// -----------------------------
enum SystemState {
  STATE_IDLE,
  STATE_RUNNING,
  STATE_EDIT
};

// -----------------------------
// Globals
// -----------------------------
MessageBus BUS;

// -----------------------------
// Cloud/MQTT Service
// -----------------------------
class CloudService : public Service {
  const char* ssid;
  const char* password;
  const char* broker;

  WiFiClient wifiClient;
  PubSubClient mqtt;

  float lastBpm = -1.0f;
  int lastState = -1;

public:
  CloudService(const char* s, const char* p, const char* b)
    : ssid(s), password(p), broker(b), mqtt(wifiClient) {}

  void init() override {
    Serial.print("[CLOUD] Connecting to WiFi");
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
      delay(300);
      Serial.print(".");
      if (millis() - start > 15000) {
        Serial.println("\n[CLOUD] WiFi timeout (continuing without cloud)");
        return;
      }
    }

    Serial.println("\n[CLOUD] WiFi connected");
    Serial.print("[CLOUD] IP: ");
    Serial.println(WiFi.localIP());

    mqtt.setServer(broker, 1883);
    reconnect();
  }

  void reconnect() {
    if (WiFi.status() != WL_CONNECTED) return;

    while (!mqtt.connected()) {
      Serial.print("[CLOUD] MQTT connecting… ");
      if (mqtt.connect("groovebox-esp32")) {
        Serial.println("connected");
      } else {
        Serial.print("failed rc=");
        Serial.println(mqtt.state());
        delay(1000);
      }
    }
  }

  bool isConnected() {
    return (WiFi.status() == WL_CONNECTED) && mqtt.connected();
  }

  void loopMqtt() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!mqtt.connected()) reconnect();
    if (mqtt.connected()) mqtt.loop();
  }

  void publishTempo(float bpm) {
    if (!isConnected()) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "{ \"bpm\": %.2f }", bpm);
    mqtt.publish("groovebox/tempo", buf);
  }

  void publishTransport(SystemState st) {
    if (!isConnected()) return;
    if (st == STATE_RUNNING) mqtt.publish("groovebox/transport", "start");
    else mqtt.publish("groovebox/transport", "stop");
  }

  void publishNote(bool on, int note, int velocity) {
    if (!isConnected()) return;
    char buf[128];
    snprintf(
      buf,
      sizeof(buf),
      "{ \"on\": %s, \"note\": %d, \"velocity\": %d }",
      on ? "true" : "false",
      note,
      velocity
    );
    mqtt.publish("groovebox/note", buf);
  }

  // called by UI/Sequencer each frame
  void tick(float bpm, SystemState st) {
    loopMqtt();

    // tempo change
    if (bpm != lastBpm) {
      publishTempo(bpm);
      lastBpm = bpm;
      Serial.print("[CLOUD] BPM -> ");
      Serial.println(bpm);
    }

    // transport change
    if ((int)st != lastState) {
      publishTransport(st);
      lastState = (int)st;
      Serial.print("[CLOUD] TRANSPORT -> ");
      Serial.println(st == STATE_RUNNING ? "start" : "stop");
    }
  }
};

// -----------------------------
// Button Service
// -----------------------------
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
      m.data1 = 0; m.data2 = 0; m.data3 = 0;
      BUS.send(m);
    }

    last = cur;
  }
};

// -----------------------------
// UI Service (state + cursor + pattern + tap tempo)
// -----------------------------
class UIService : public Service {
public:
  SystemState state = STATE_IDLE;

  bool pattern[NUM_STEPS] = { false };
  int cursor = 0;

  // tempo
  float bpm = 120.0f;
  unsigned long stepIntervalMs = 500;   // ms per step
  unsigned long lastTapTime = 0;

  // edit UX
  unsigned long lastEditShort = 0;
  const unsigned long doubleTapWindow = 400;

  void init() override {
    Serial.println("[UI] init()");
  }

  unsigned long getStepInterval() const {
    return stepIntervalMs;
  }

  int page() const {
    return cursor / VISIBLE_STEPS;
  }

  void handleTapTempo() {
    if (state != STATE_RUNNING) return;

    unsigned long now = millis();
    if (lastTapTime != 0) {
      unsigned long delta = now - lastTapTime;

      // sanity window
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

            // double short exits edit
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
          if (state == STATE_RUNNING) {
            state = STATE_EDIT;
            Serial.println("[UI] State -> EDIT");
          }
          else if (state == STATE_EDIT) {
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
  }
};

// -----------------------------
// LED Grid Service (4 LEDs show current page)
// -----------------------------
class LEDGridService : public Service {
  int pins[VISIBLE_STEPS];
  UIService* ui;

  unsigned long lastBlink = 0;
  bool blink = false;

public:
  int currentStep = -1;

  LEDGridService(int p0, int p1, int p2, int p3, UIService* u)
    : ui(u)
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

    int baseStep = ui->page() * VISIBLE_STEPS;

    for (int i = 0; i < VISIBLE_STEPS; i++) {
      int stepIndex = baseStep + i;
      bool on = false;

      if (ui->state == STATE_RUNNING) {
        if (stepIndex == currentStep) on = true;
        else if (stepIndex < NUM_STEPS && ui->pattern[stepIndex]) on = blink;
      }
      else if (ui->state == STATE_EDIT) {
        if (stepIndex == ui->cursor) on = true;
        else if (stepIndex < NUM_STEPS && ui->pattern[stepIndex]) on = blink;
      }

      digitalWrite(pins[i], on ? HIGH : LOW);
    }
  }
};

// -----------------------------
// Sequencer Service (clocked)
// -----------------------------
class SequencerService : public Service {
  UIService* ui;
  LEDGridService* leds;
  CloudService* cloud;

  unsigned long lastStep = 0;
  int step = 0;

  // MIDI mapping (Kick for now)
  const int MIDI_NOTE = 36;
  const int VELOCITY  = 100;

  // note-off timing
  bool noteActive = false;
  unsigned long noteOffAt = 0;

public:
  SequencerService(UIService* u, LEDGridService* l, CloudService* c)
    : ui(u), leds(l), cloud(c) {}

  void init() override {
    Serial.println("[SEQ] init()");
  }

  void update() override {
    // keep MQTT alive + publish tempo/transport continuously
    cloud->tick(ui->bpm, ui->state);

    if (ui->state != STATE_RUNNING) {
      // if we stop while a note is active, send off
      if (noteActive) {
        cloud->publishNote(false, MIDI_NOTE, 0);
        noteActive = false;
      }
      return;
    }

    unsigned long now = millis();
    unsigned long interval = ui->getStepInterval();
    if (interval < 50) interval = 50;

    // send note-off if time
    if (noteActive && now >= noteOffAt) {
      cloud->publishNote(false, MIDI_NOTE, 0);
      noteActive = false;
    }

    if (now - lastStep < interval) return;
    lastStep = now;

    leds->currentStep = step;

    if (ui->pattern[step]) {
      Serial.print("[SEQ] Step ");
      Serial.print(step);
      Serial.println(" TRIGGER");

      cloud->publishNote(true, MIDI_NOTE, VELOCITY);
      noteActive = true;
      noteOffAt = now + 60; // short drum hit
    }

    step = (step + 1) % NUM_STEPS;
  }
};

// -----------------------------
// WiFi/MQTT credentials
// -----------------------------
const char* WIFI_SSID = "Silverado@2015";
const char* WIFI_PASS = "Krishveer@2005";
const char* MQTT_BROKER = "10.0.0.126"; // <-- your Linux IP running mosquitto

// -----------------------------
// Services
// -----------------------------
ButtonService button(16);
UIService ui;
LEDGridService leds(17, 4, 5, 18, &ui);
CloudService cloud(WIFI_SSID, WIFI_PASS, MQTT_BROKER);
SequencerService sequencer(&ui, &leds, &cloud);

Service* services[] = {
  &button,
  &ui,
  &leds,
  &cloud,
  &sequencer
};

const int NUM_SERVICES = sizeof(services) / sizeof(Service*);

// -----------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

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
