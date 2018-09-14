#!/usr/bin/bash

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 
   exit 1
fi

make power-meter || exit(1)
install -g dialup -m 776 power-meter /opt/power-meter
install -g root -o root -m 744 power-meter.service 
