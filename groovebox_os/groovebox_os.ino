#include "service_base.h"
#include "message_bus.h"
#include "services.h"

// Instantiate global MessageBus
MessageBus BUS;

LEDService led(16);
ButtonService button(17);
UIService ui;
SequencerService sequencer(&ui);
LogService logger;

Service* services[] = {
    &led,
    &button,
    &ui,
    &sequencer,
    &logger
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
