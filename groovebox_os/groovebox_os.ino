#include <Arduino.h>
#include "service_base.h"
#include "message_bus.h"
#include "services.h"

const char* WIFI_SSID = "Silverado@2015";
const char* WIFI_PASS = "Krishveer@2005";
const char* BROKER_IP = "10.0.0.126";




// --------------------- GLOBAL MESSAGE BUS ---------------------
MessageBus BUS;

// --------------------- CREATE SERVICES -----------------------
ButtonService button(16);              // Your working button pin
UIService ui;
AudioService audio(25);                // AUDIO OUT on GPIO25 → resistor → headphones
LEDGridService ledGrid(17, 4, 5, 18, &ui.state, &ui.cursor, ui.pattern);
LogService logger;
CloudService cloud(WIFI_SSID, WIFI_PASS, BROKER_IP, &ui);
SequencerService sequencer(&ui, &leds, &audio, &cloud);

// --------------------- SERVICE TABLE -------------------------
Service* services[] = {
    &button,
    &ui,
    &sequencer,
    &audio,
    &ledGrid,
    &cloud,
    &logger
};


const int NUM_SERVICES = sizeof(services) / sizeof(Service*);

// --------------------- SETUP --------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n[BOOT] Groovebox OS Starting...");
    for (int i = 0; i < NUM_SERVICES; i++) {
        services[i]->init();
    }
    Serial.println("[BOOT] All services initialized.\n");
}

// --------------------- MAIN LOOP -----------------------------
void loop() {
    for (int i = 0; i < NUM_SERVICES; i++) {
        services[i]->update();
    }
}
