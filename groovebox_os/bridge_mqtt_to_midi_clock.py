import json
import time
import threading
from typing import Optional

import paho.mqtt.client as mqtt
import mido

# -----------------------
# CONFIG (EDIT THESE)
# -----------------------
BROKER_HOST = "10.0.0.126"   # your Mosquitto IP (Linux box)
BROKER_PORT = 1883

MQTT_TOPIC = "groovebox/#"

# MIDI output port selection
# If you only see "Midi Through", set this to "Midi Through"
# On Windows with loopMIDI, set it to your loopMIDI port name (e.g. "Groovebox")
MIDI_OUT_NAME_CONTAINS = "Groovebox"

# MIDI channel 1 = 0 in mido
MIDI_CHANNEL = 0

# Gate time for note_off if you only receive note_on
DEFAULT_GATE_MS = 60

# -----------------------
# State
# -----------------------
running = False
bpm = 120.0
midi_out: Optional[mido.ports.BaseOutput] = None


def find_midi_out():
    names = mido.get_output_names()
    if not names:
        raise RuntimeError("No MIDI output ports found.")
    for n in names:
        if MIDI_OUT_NAME_CONTAINS.lower() in n.lower():
            print(f"[MIDI] Using output: {n}")
            return mido.open_output(n)
    # fallback: if only one port exists, use it
    if len(names) == 1:
        print(f"[MIDI] Using only available output: {names[0]}")
        return mido.open_output(names[0])
    raise RuntimeError(f"Could not find MIDI out containing '{MIDI_OUT_NAME_CONTAINS}'. Available: {names}")


def send_start():
    global midi_out
    if midi_out is None:
        return
    midi_out.send(mido.Message('start'))
    print("[MIDI] START")


def send_stop():
    global midi_out
    if midi_out is None:
        return
    midi_out.send(mido.Message('stop'))
    print("[MIDI] STOP")


def send_note(note: int, velocity: int, on: bool):
    global midi_out
    if midi_out is None:
        return
    if on:
        midi_out.send(mido.Message('note_on', note=note, velocity=velocity, channel=MIDI_CHANNEL))
        print(f"[MIDI] NOTE ON  note={note} vel={velocity}")
    else:
        midi_out.send(mido.Message('note_off', note=note, velocity=0, channel=MIDI_CHANNEL))
        print(f"[MIDI] NOTE OFF note={note}")


def schedule_note_off(note: int, delay_ms: int):
    def _worker():
        time.sleep(delay_ms / 1000.0)
        send_note(note, 0, False)
    threading.Thread(target=_worker, daemon=True).start()


def clock_thread():
    """Send MIDI clock (24 PPQN) while running, using current BPM."""
    global running, bpm, midi_out

    next_t = time.perf_counter()
    while True:
        if midi_out is None:
            time.sleep(0.2)
            continue

        if not running:
            time.sleep(0.01)
            next_t = time.perf_counter()
            continue

        # MIDI clock tick interval: 60s / (BPM * 24)
        tick = 60.0 / (max(1.0, bpm) * 24.0)

        now = time.perf_counter()
        if now >= next_t:
            midi_out.send(mido.Message('clock'))
            next_t += tick
        else:
            # sleep a fraction to reduce CPU
            time.sleep(min(0.001, max(0.0, next_t - now)))


def parse_json_maybe(payload: str):
    payload = payload.strip()
    if not payload:
        return None
    if payload[0] in "{[":
        try:
            return json.loads(payload)
        except json.JSONDecodeError:
            return None
    return None


def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] Connected rc={rc}")
    client.subscribe(MQTT_TOPIC)
    print(f"[MQTT] Subscribed to {MQTT_TOPIC}")


def on_message(client, userdata, msg):
    global running, bpm

    topic = msg.topic
    payload = msg.payload.decode(errors="ignore").strip()

    # transport can be plain "start"/"stop"
    if topic == "groovebox/transport":
        if payload.lower() == "start":
            running = True
            send_start()
        elif payload.lower() == "stop":
            running = False
            send_stop()
        else:
            # also allow JSON { "state": "start" }
            data = parse_json_maybe(payload)
            if isinstance(data, dict) and "state" in data:
                st = str(data["state"]).lower()
                if st == "start":
                    running = True
                    send_start()
                elif st == "stop":
                    running = False
                    send_stop()
        return

    if topic == "groovebox/tempo":
        data = parse_json_maybe(payload)
        if isinstance(data, dict) and "bpm" in data:
            try:
                bpm = float(data["bpm"])
                print(f"[MQTT] BPM -> {bpm:.2f}")
            except ValueError:
                pass
        return

    if topic == "groovebox/note":
        data = parse_json_maybe(payload)
        if isinstance(data, dict):
            try:
                note = int(data.get("note", 36))
                vel = int(data.get("velocity", 100))
                on = bool(data.get("on", True))
                send_note(note, vel, on)

                # If it's a note_on and you don't also get note_off events, auto-gate
                if on and "on" in data and data["on"] is True:
                    # only gate if your ESP isn't sending note_off (but it is).
                    # still safe — note_off twice is fine in FL.
                    schedule_note_off(note, DEFAULT_GATE_MS)
            except Exception as e:
                print(f"[MQTT] bad note payload: {payload} ({e})")
        return


def main():
    global midi_out
    midi_out = find_midi_out()

    # start clock sender thread
    threading.Thread(target=clock_thread, daemon=True).start()

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[MQTT] Connecting to {BROKER_HOST}:{BROKER_PORT} ...")
    client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
    client.loop_forever()


if __name__ == "__main__":
    main()
