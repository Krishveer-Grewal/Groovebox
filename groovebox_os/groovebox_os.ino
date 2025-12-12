#include <Arduino.h>
#include "service_base.h"
#include "message_bus.h"
#include "services.h"

// --------------------- GLOBAL MESSAGE BUS ---------------------
MessageBus BUS;

// --------------------- CREATE SERVICES -----------------------
ButtonService button(16);              // Your working button pin
UIService ui;
AudioService audio(25);                // AUDIO OUT on GPIO25 → resistor → headphones
LEDGridService ledGrid(17, 4, 5, 18, &ui.state, &ui.cursor, ui.pattern);
SequencerService sequencer(&ui, &ledGrid, &audio);
LogService logger;

// --------------------- SERVICE TABLE -------------------------
Service* services[] = {
    &button,
    &ui,
    &sequencer,
    &audio,
    &ledGrid,
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
