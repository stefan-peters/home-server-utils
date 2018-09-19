#!/usr/bin/bash

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 
   exit 1
fi

make power-meter

install -g dialout -m 776 power-meter /opt/power-meter
install -g root -o root -m 744 power-meter.service /etc/systemd/system
systemctl daemon-reload
systemctl start power-meter.service
systemctl enable power-meter.service


