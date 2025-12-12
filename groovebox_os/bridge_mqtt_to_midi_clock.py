import time
import json
import threading
import paho.mqtt.client as mqtt
import mido

BROKER_HOST = "127.0.0.1"
BROKER_PORT = 1883

MIDI_OUT_NAME_CONTAINS = "Groovebox"  # e.g. "Groovebox Clock" (loopMIDI)
DEFAULT_BPM = 120.0

state = {
    "running": False,
    "bpm": DEFAULT_BPM,
}

def find_midi_out():
    names = mido.get_output_names()
    for n in names:
        if MIDI_OUT_NAME_CONTAINS.lower() in n.lower():
            return mido.open_output(n)
    raise RuntimeError(f"Could not find MIDI out containing '{MIDI_OUT_NAME_CONTAINS}'. उपलब्ध: {names}")

midi_out = find_midi_out()

def send_start():
    midi_out.send(mido.Message.from_bytes([0xFA]))

def send_stop():
    midi_out.send(mido.Message.from_bytes([0xFC]))

def send_clock_tick():
    midi_out.send(mido.Message.from_bytes([0xF8]))

def clock_thread():
    # 24 PPQN -> ticks per second = bpm * 24 / 60
    while True:
        if state["running"]:
            bpm = max(20.0, min(300.0, state["bpm"]))
            tick_hz = bpm * 24.0 / 60.0
            interval = 1.0 / tick_hz
            send_clock_tick()
            time.sleep(interval)
        else:
            time.sleep(0.01)

def on_connect(client, userdata, flags, rc):
    client.subscribe("groovebox/tempo")
    client.subscribe("groovebox/transport")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        data = json.loads(payload) if payload.strip().startswith("{") else payload
    except Exception:
        return

    if msg.topic == "groovebox/tempo":
        if isinstance(data, dict) and "bpm" in data:
            state["bpm"] = float(data["bpm"])
            print(f"[BRIDGE] BPM = {state['bpm']}")
    elif msg.topic == "groovebox/transport":
        if isinstance(data, dict) and data.get("state") == "start":
            if not state["running"]:
                state["running"] = True
                send_start()
                print("[BRIDGE] START")
        elif isinstance(data, dict) and data.get("state") == "stop":
            if state["running"]:
                state["running"] = False
                send_stop()
                print("[BRIDGE] STOP")

t = threading.Thread(target=clock_thread, daemon=True)
t.start()

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER_HOST, BROKER_PORT, 60)
print("[BRIDGE] Connected. Listening…")
client.loop_forever()
