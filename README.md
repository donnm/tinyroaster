# tinyroaster

Arduino sketch for ATtiny85 or similar to be used in a coffee roasting controller board.

The design uses an ATtiny85 (see sketch for pinouts), MAX6675 temperature amplifier and type K thermocouple, 74HC165 shift register for interfacing with a Hitatchi compatible LCD in 4-bit mode.

The controller board manages timing, heater (via relay) and fan speed (~20V PWM) of a modified hot-air popcorn maker to roast small quantities of beans (40-100g).

Hardware design files to come soon...
