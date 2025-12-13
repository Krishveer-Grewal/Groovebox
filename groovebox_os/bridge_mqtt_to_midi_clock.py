import time, json, threading
import paho.mqtt.client as mqtt
import mido

BROKER = "127.0.0.1"
PORT = 1883
MIDI_OUT_CONTAINS = "Groovebox"

state = {"bpm":120.0,"running":False}

def midi_out():
    for n in mido.get_output_names():
        if MIDI_OUT_CONTAINS.lower() in n.lower():
            return mido.open_output(n)
    raise RuntimeError("MIDI out not found")

midi = midi_out()

def clock():
    while True:
        if state["running"]:
            interval = 60.0/(state["bpm"]*24)
            midi.send(mido.Message.from_bytes([0xF8]))
            time.sleep(interval)
        else:
            time.sleep(0.01)

def on_msg(c,u,m):
    payload = m.payload.decode()
    if m.topic=="groovebox/tempo":
        state["bpm"]=json.loads(payload)["bpm"]
    elif m.topic=="groovebox/transport":
        if payload=="start":
            state["running"]=True
            midi.send(mido.Message.from_bytes([0xFA]))
        else:
            state["running"]=False
            midi.send(mido.Message.from_bytes([0xFC]))
    elif m.topic=="groovebox/note":
        d=json.loads(payload)
        midi.send(mido.Message("note_on",
            note=d["note"],velocity=d["velocity"],channel=0))

threading.Thread(target=clock,daemon=True).start()

c=mqtt.Client()
c.on_message=on_msg
c.connect(BROKER,PORT,60)
c.subscribe("groovebox/#")
c.loop_forever()
