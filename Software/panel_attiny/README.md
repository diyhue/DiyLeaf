# Mandatory to flash the binary file from this repo with avrdude and following arguments:

/home/marius/.arduino15/packages/arduino/tools/avrdude/6.3.0-arduino17/bin/avrdude -C/home/marius/.arduino15/packages/ATTinyCore/hardware/avr/1.3.1/avrdude.conf -v -pattiny85 -carduino -P/dev/ttyUSB0 -b19200 -e -Uefuse:w:0xFF:m -Uhfuse:w:0b01010111:m -Ulfuse:w:0xF1:m -Uflash:w:/home/marius/Documents/panel_attiny/panel_attiny/panel_attiny.ino.hex:i
