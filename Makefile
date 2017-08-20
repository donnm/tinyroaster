AVRDUDE_ARD_PROGRAMMER = buspirate
AVRDUDE_ARD_BAUDRATE = 115200
AVRDUDE_OPTS = -v -C/opt/arduino-1.6.12/hardware/tools/avr/etc/avrdude.conf
HEX_MAXIMUM_SIZE = 8912
ISP_PORT							= /dev/ttyUSB5
BOARD_TAG      = attiny
BOARD_SUB      = attiny85
ALTERNATE_CORE = attiny
ALTERNATE_CORE_PATH = /home/donn/.arduino15/packages/attiny/hardware/avr/1.0.2
ARDUINO_VAR_PATH = /home/donn/.arduino15/packages/attiny/hardware/avr/1.0.2/variants/tiny8
ARDUINO_LIBS = new-liquidcrystal MAX6675_library SendOnlySoftwareSerial PID TinyWireM EEPROM
MCU = attiny85
F_CPU          = 8000000L
ARDUINO_DIR    = /opt/arduino-1.6.12
include Arduino.mk

