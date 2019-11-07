#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>


uint8_t colors[3], i2cBuffer[10], left, right;
float step_level[3], current_colors[3];
bool inTransition, configured;


void(* resetFunc) (void) = 0;

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(18, 1, NEO_GRB + NEO_KHZ800);


void setPanelColor(int8_t r, int8_t g, int8_t b) {
  for (int8_t i = 0; i < 18; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void lightEngine() {
  for (uint8_t color = 0; color < 3; color++) {
    if (colors[color] != current_colors[color] ) {
      inTransition = true;
      current_colors[color] += step_level[color];
      if ((step_level[color] > 0.0f && current_colors[color] > colors[color]) || (step_level[color] < 0.0f && current_colors[color] < colors[color])) current_colors[color] = colors[color];
    }
  }
  if (inTransition) {
    setPanelColor(current_colors[0], current_colors[1], current_colors[2]);
    inTransition = false;
  }
}


void applyColor(uint8_t r, uint8_t g, uint8_t b) {
  colors[0] = r; colors[1] = g; colors[2] = b;
}

void process_lightdata(int transitiontime) {
  for (uint8_t color = 0; color < 3; color++) {
    step_level[color] = (float (colors[color]) - current_colors[color]) / float (transitiontime * 1);
  }
}

void notifyLeft() {
  pinMode(left, OUTPUT);
  digitalWrite(left, LOW);
  delay(100);
  pinMode(left, INPUT_PULLUP);
}

void notifyRight() {
  pinMode(right, OUTPUT);
  digitalWrite(right, LOW);
  delay(100);
  pinMode(right, INPUT_PULLUP);
}

void applyColorToAll(int8_t r, int8_t g, int8_t b, int transitiontime = 4) {
  for (int8_t edge = 0; edge < 3; edge++) {
    colors[0] = r;
    colors[1] = g;
    colors[2] = b;
    process_lightdata(transitiontime);
  }
}

void setNewAddress(uint8_t howMany)
{
  EEPROM.write(0x00, 1);
  EEPROM.write(0x01, Wire.read());
  delay(50);
  resetFunc();
}


void receiveEvent(uint8_t howMany)
{
  uint8_t packet = 0;
  while (Wire.available() > 0) {
    i2cBuffer[packet] = Wire.read();
    packet++;
    //Serial.println("packets set " + (String)packet);
  }
  if (howMany == 2)
  {
    //Serial.println("received 2 bytes");
    if (i2cBuffer[0] == 252) {
      if (i2cBuffer[1] == 250) {
        applyColor(0, 25, 25);
        process_lightdata(4);
        notifyLeft();
      } else if (i2cBuffer[1] == 251) {
        applyColor(25, 20, 30);
        process_lightdata(4);
        notifyRight();
      } else if (i2cBuffer[1] == 252) {
        delay(50);
        resetFunc();
      } else {
        //Serial.println("change i2c address to " + (String)i2cBuffer[1]);
        applyColorToAll(25, 0, 25);
        Wire.begin(i2cBuffer[1]);
        Wire.onReceive(receiveEvent);
      }
    }
  }

  else if (howMany == 3) {
    //Serial.println("received 3 bytes");
    setPanelColor(i2cBuffer[0], i2cBuffer[1], i2cBuffer[2]);
  }

  else if (howMany == 4) {
    //Serial.println("received 4 bytes");
    applyColor(i2cBuffer[0],i2cBuffer[1],i2cBuffer[2]);
    process_lightdata(4);
    //setPanelColor(i2cBuffer[0], i2cBuffer[1], i2cBuffer[2]);
  }
}

void setup()
{
  //Serial.begin(9600);
  //Serial.println("starting...");
  pinMode(3, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);

  pixels.begin();

  if (EEPROM.read(0x00) == 1) {
    Wire.begin(EEPROM.read(0x01));
    Wire.onReceive(receiveEvent);
    EEPROM.write(0x00, 0);
    applyColor(0, 50, 0);
    if (EEPROM.read(0x02) == 3) {
      left = 4; right = 5;
    } else if (EEPROM.read(0x02) == 4) {
      left = 5; right = 3;
    } else {
      left = 3; right = 4;
    }
    configured = true;
  } else {
    applyColor(50, 0, 0);
  }
  process_lightdata(4);
}

void loop()
{
  lightEngine();
  if (!configured) {
    while (digitalRead(3) == HIGH && digitalRead(4) == HIGH && digitalRead(5) == HIGH) {
      lightEngine();
    }

    if (digitalRead(3) == LOW) {
      EEPROM.write(0x02, 3);
    } else if (digitalRead(4) == LOW) {
      EEPROM.write(0x02, 4);
    } else if (digitalRead(5) == LOW) {
      EEPROM.write(0x02, 5);
    }

    Wire.begin(0x7E);
    Wire.onReceive(setNewAddress);

    applyColor(25, 25, 0);
    process_lightdata(4);
    configured = true;
  }
  delay(50);
}
