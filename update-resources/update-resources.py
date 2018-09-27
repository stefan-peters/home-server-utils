#!/usr/bin/python3

import re
import time
import sys
import paho.mqtt.client as mqtt
from influxdb import InfluxDBClient

def on_connect(client, userdata, flags, rc):

    # wait and exit (and be restarted by systemd)
    if rc != 0:
        time.sleep(5)
        sys.exit(2)

    print("Connected!")
    client.subscribe("resources/power/#")

def on_message(client, db, msg):
    #print(msg.topic, msg.payload.decode('utf-8'))

    json_body = [{"measurement": "power","fields": {}}]

    m = re.search(r'\d+(\.\d+)?', msg.payload.decode("utf-8"))

    if not m:
        return

    value = float(m.group(0))

    if "current" in msg.topic:    
        json_body[0]["fields"]["current"] = value
    else:
        # watts to kilowatt
        json_body[0]["fields"]["total"] = round(value / 1000, 3)

    db.write_points(json_body)

if __name__ == "__main__":
    db = InfluxDBClient('192.168.1.17', 8086, 'root', 'root', 'resources')
    db.create_database("resources")

    client = mqtt.Client(userdata=db)
    client.on_connect = on_connect
    client.on_message = on_message

    # ensure we are restarted by systemd
    client.on_disconnect = lambda x: sys.exit(1)

    client.connect("mqtt-broker.iotnet")
    client.loop_forever()