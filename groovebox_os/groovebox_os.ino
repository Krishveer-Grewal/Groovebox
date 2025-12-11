#include "service_base.h"
#include "message_bus.h"
#include "services.h"

// Instantiate global MessageBus
MessageBus BUS;

ButtonService button(16);
UIService ui;
LogService logger;
LEDGridService ledGrid(17, 4, 5, 18
    , &ui.state, &ui.cursor, ui.pattern);
SequencerService sequencer(&ui, &ledGrid);

Service* services[] = {
    &button,
    &ui,
    &ledGrid,
    &sequencer
};



const int NUM_SERVICES = sizeof(services) / sizeof(Service*);

// ---------------------- SETUP -----------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[BOOT] Groovebox OS Starting...");

    for (int i = 0; i < NUM_SERVICES; i++)
        services[i]->init();

    Serial.println("[BOOT] Services initialized successfully.");
}

// ---------------------- MAIN LOOP -------------------
void loop() {
    for (int i = 0; i < NUM_SERVICES; i++) {
        services[i]->update();
    }
}
