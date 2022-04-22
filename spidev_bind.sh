#!/bin/sh

echo spidev > /sys/bus/spi/devices/spi0.1/driver_override
echo spi0.1 > /sys/bus/spi/drivers/spidev/bind


