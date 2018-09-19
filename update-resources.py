#!/usr/bin/python3

import re
import paho.mqtt.client as mqtt
from influxdb import InfluxDBClient

def on_connect(client, userdata, flags, rc):
    assert(rc == 0)
    print("Connected!")
    client.subscribe("resources/power/#")

def on_message(client, userdata, msg):
    #print(msg.topic, msg.payload.decode('utf-8'))

    json_body = [{"measurement": "power","fields": {}}]

    if "current" in msg.topic:
        value = re.search(r'\d+(\.\d+)?', msg.payload.decode("utf-8"))
        json_body[0]["fields"]["current"] = float(value.group(0))

    else:
        value = re.search(r'\d+(\.\d+)?', msg.payload.decode("utf-8"))
        json_body[0]["fields"]["total"] = round(float(value.group(0)) / 1000, 3)

    db.write_points(json_body)

if __name__ == "__main__":
    db = InfluxDBClient('192.168.1.17', 8086, 'root', 'root', 'resources')
    db.create_database("resources")

    client = mqtt.Client(userdata=db)
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = lambda x: sys.exit(1)

    client.connect("mqtt-broker.iotnet")
    client.loop_forever()