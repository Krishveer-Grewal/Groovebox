import json
import time
import threading

import paho.mqtt.client as mqtt
import mido

# ----------------------------
# CONFIG
# ----------------------------
BROKER_HOST = "10.0.0.126"   # Linux mosquitto IP (same as MQTT_BROKER in ESP32)
BROKER_PORT = 1883

# If this substring is found in a MIDI output name, we use it.
# On Windows, for FL, choose your loopMIDI port name substring (e.g. "Groovebox" or "loopMIDI").
MIDI_OUT_NAME_CONTAINS = "loopMIDI"   # change to match your port name

TOPIC_TEMPO = "groovebox/tempo"
TOPIC_TRANSPORT = "groovebox/transport"
TOPIC_NOTE = "groovebox/note"

# ----------------------------
# MIDI Clock Engine
# ----------------------------
class MidiClock:
    def __init__(self, midi_out):
        self.midi_out = midi_out
        self.bpm = 120.0
        self.running = False
        self._lock = threading.Lock()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def set_bpm(self, bpm: float):
        with self._lock:
            self.bpm = max(20.0, min(300.0, float(bpm)))

    def start(self):
        with self._lock:
            if not self.running:
                self.running = True
                self.midi_out.send(mido.Message("start"))

    def stop(self):
        with self._lock:
            if self.running:
                self.running = False
                self.midi_out.send(mido.Message("stop"))

    def _run(self):
        # MIDI clock is 24 pulses per quarter note.
        # Pulse interval = 60 / (BPM * 24)
        next_time = time.perf_counter()
        while True:
            with self._lock:
                bpm = self.bpm
                running = self.running

            if running:
                interval = 60.0 / (bpm * 24.0)
                now = time.perf_counter()
                if now >= next_time:
                    self.midi_out.send(mido.Message("clock"))
                    next_time += interval
                else:
                    time.sleep(min(0.001, next_time - now))
            else:
                time.sleep(0.01)
                next_time = time.perf_counter()

# ----------------------------
# MIDI OUT selection
# ----------------------------
def find_midi_out():
    names = mido.get_output_names()
    if not names:
        raise RuntimeError("No MIDI output ports found.")

    print("Available MIDI OUT ports:")
    for i, n in enumerate(names):
        print(f"  [{i}] {n}")

    for n in names:
        if MIDI_OUT_NAME_CONTAINS.lower() in n.lower():
            print(f"\nUsing MIDI OUT: {n}")
            return mido.open_output(n)

    raise RuntimeError(f"Could not find MIDI out containing '{MIDI_OUT_NAME_CONTAINS}'. Available: {names}")

# ----------------------------
# Main
# ----------------------------
midi_out = find_midi_out()
clock = MidiClock(midi_out)

def on_connect(client, userdata, flags, rc):
    print("[MQTT] connected:", rc)
    client.subscribe(TOPIC_TEMPO)
    client.subscribe(TOPIC_TRANSPORT)
    client.subscribe(TOPIC_NOTE)

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode(errors="ignore").strip()

    if topic == TOPIC_TEMPO:
        try:
            data = json.loads(payload)
            bpm = float(data.get("bpm", 120.0))
            clock.set_bpm(bpm)
            print(f"[TEMPO] bpm={bpm:.2f}")
        except Exception as e:
            print("[TEMPO] parse error:", e, payload)

    elif topic == TOPIC_TRANSPORT:
        # payload is "start" or "stop"
        if payload == "start":
            print("[TRANSPORT] start")
            clock.start()
        elif payload == "stop":
            print("[TRANSPORT] stop")
            clock.stop()
        else:
            print("[TRANSPORT] unknown:", payload)

    elif topic == TOPIC_NOTE:
        try:
            data = json.loads(payload)
            on = bool(data.get("on", True))
            note = int(data.get("note", 36))
            vel = int(data.get("velocity", 100))

            if on:
                midi_out.send(mido.Message("note_on", note=note, velocity=max(1, min(127, vel)), channel=0))
                print(f"[NOTE] ON  note={note} vel={vel}")
            else:
                midi_out.send(mido.Message("note_off", note=note, velocity=0, channel=0))
                print(f"[NOTE] OFF note={note}")
        except Exception as e:
            print("[NOTE] parse error:", e, payload)

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

print(f"[MQTT] connecting to {BROKER_HOST}:{BROKER_PORT} ...")
client.connect(BROKER_HOST, BROKER_PORT, 60)
client.loop_forever()
