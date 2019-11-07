#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <NeoPixelBus.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Wire.h>


#define light_name "NanoLeaf Hue" //default light name
#define maxpanelsCount 20

#define LIGHT_VERSION 3.1
#define LIGHT_NAME_MAX_LENGTH 32 // Longer name will get stripped
#define ENTERTAINMENT_TIMEOUT 1500 // millis

IPAddress address ( 192,  168,   0,  95); // choose an unique IP Adress
IPAddress gateway ( 192,  168,   0,   1); // Router IP
IPAddress submask(255, 255, 255,   0);

NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(1);

RgbColor red = RgbColor(50, 0, 0);
RgbColor green = RgbColor(0, 50, 0);
RgbColor white = RgbColor(25, 25, 25);
RgbColor yellow = RgbColor(25, 25, 0);
RgbColor magenta = RgbColor(25, 0, 25);
RgbColor black = RgbColor(0);

struct state {
  int ct = 200, hue;
  uint8_t colors[3], bri = 100, sat = 254, colorMode = 2, direction, position_x, position_y;
  float x, y;
  bool lightState, reachable;
};

uint8_t error;
state panels[maxpanelsCount];

unsigned long lastEPMillis;
byte mac[6];
byte packetBuffer[64];

const byte color_red[] = {50, 0, 0, 4};
const byte color_green[] = {0, 50, 0, 4};
const byte color_blue[] = {0, 0, 50, 4};

const byte color_1[] = {200, 100, 0};
const byte color_2[] = {50, 200, 100};
const byte color_3[] = {200, 200, 200};

ESP8266WebServer server(80);
WiFiUDP Udp;
ESP8266HTTPUpdateServer httpUpdateServer;


void blinkLed(uint8_t count, RgbColor color, int interval = 200) {
  strip.SetPixelColor(0, black);
  strip.Show();
  delay(interval);
  for (uint8_t i = 0; i < count; i++) {
    strip.SetPixelColor(0, color);
    strip.Show();
    delay(interval);
    strip.SetPixelColor(0, black);
    strip.Show();
    delay(interval);
  }
  strip.SetPixelColor(0, white);
  strip.Show();
}

void convert_hue(uint8_t panel)
{
  double      hh, p, q, t, ff, s, v;
  long        i;

  s = panels[panel].sat / 255.0;
  v = panels[panel].bri / 255.0;

  if (s <= 0.0) {      // < is bogus, just shuts up warnings
    panels[panel].colors[0] = v;
    panels[panel].colors[1] = v;
    panels[panel].colors[2] = v;
    return;
  }
  hh = panels[panel].hue;
  if (hh >= 65535.0) hh = 0.0;
  hh /= 11850, 0;
  i = (long)hh;
  ff = hh - i;
  p = v * (1.0 - s);
  q = v * (1.0 - (s * ff));
  t = v * (1.0 - (s * (1.0 - ff)));

  switch (i) {
    case 0:
      panels[panel].colors[0] = v * 255.0;
      panels[panel].colors[1] = t * 255.0;
      panels[panel].colors[2] = p * 255.0;
      break;
    case 1:
      panels[panel].colors[0] = q * 255.0;
      panels[panel].colors[1] = v * 255.0;
      panels[panel].colors[2] = p * 255.0;
      break;
    case 2:
      panels[panel].colors[0] = p * 255.0;
      panels[panel].colors[1] = v * 255.0;
      panels[panel].colors[2] = t * 255.0;
      break;

    case 3:
      panels[panel].colors[0] = p * 255.0;
      panels[panel].colors[1] = q * 255.0;
      panels[panel].colors[2] = v * 255.0;
      break;
    case 4:
      panels[panel].colors[0] = t * 255.0;
      panels[panel].colors[1] = p * 255.0;
      panels[panel].colors[2] = v * 255.0;
      break;
    case 5:
    default:
      panels[panel].colors[0] = v * 255.0;
      panels[panel].colors[1] = p * 255.0;
      panels[panel].colors[2] = q * 255.0;
      break;
  }

}

void convert_xy(uint8_t panel)
{
  int optimal_bri = panels[panel].bri;
  if (optimal_bri < 5) {
    optimal_bri = 5;
  }
  float Y = panels[panel].y;
  float X = panels[panel].x;
  float Z = 1.0f - panels[panel].x - panels[panel].y;

  // sRGB D65 conversion
  float r =  X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
  float g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
  float b =  X * 0.0557f - Y * 0.2040f + Z * 1.0570f;


  // Apply gamma correction
  r = r <= 0.04045f ? r / 12.92f : pow((r + 0.055f) / (1.0f + 0.055f), 2.4f);
  g = g <= 0.04045f ? g / 12.92f : pow((g + 0.055f) / (1.0f + 0.055f), 2.4f);
  b = b <= 0.04045f ? b / 12.92f : pow((b + 0.055f) / (1.0f + 0.055f), 2.4f);

  if (r > b && r > g) {
    // red is biggest
    if (r > 1.0f) {
      g = g / r;
      b = b / r;
      r = 1.0f;
    }
  }
  else if (g > b && g > r) {
    // green is biggest
    if (g > 1.0f) {
      r = r / g;
      b = b / g;
      g = 1.0f;
    }
  }
  else if (b > r && b > g) {
    // blue is biggest
    if (b > 1.0f) {
      r = r / b;
      g = g / b;
      b = 1.0f;
    }
  }

  r = r < 0 ? 0 : r;
  g = g < 0 ? 0 : g;
  b = b < 0 ? 0 : b;

  panels[panel].colors[0] = (int) (r * optimal_bri); panels[panel].colors[1] = (int) (g * optimal_bri); panels[panel].colors[2] = (int) (b * optimal_bri);
  //Serial.println("apply XY" + String(panel));
}

void convert_ct(uint8_t panel) {
  int hectemp = 10000 / panels[panel].ct;
  int r, g, b;
  if (hectemp <= 66) {
    r = 255;
    g = 99.4708025861 * log(hectemp) - 161.1195681661;
    b = hectemp <= 19 ? 0 : (138.5177312231 * log(hectemp - 10) - 305.0447927307);
  } else {
    r = 329.698727446 * pow(hectemp - 60, -0.1332047592);
    g = 288.1221695283 * pow(hectemp - 60, -0.0755148492);
    b = 255;
  }
  r = r > 255 ? 255 : r;
  g = g > 255 ? 255 : g;
  b = b > 255 ? 255 : b;
  panels[panel].colors[0] = r * (panels[panel].bri / 255.0f); panels[panel].colors[1] = g * (panels[panel].bri / 255.0f); panels[panel].colors[2] = b * (panels[panel].bri / 255.0f);
  //Serial.println("apply CT " + String(panel));
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}


void processLightdata(uint8_t panel) {
  if (panels[panel].colorMode == 1 && panels[panel].lightState == true) {
    convert_xy(panel);
  } else if (panels[panel].colorMode == 2 && panels[panel].lightState == true) {
    convert_ct(panel);
  } else if (panels[panel].colorMode == 3 && panels[panel].lightState == true) {
    convert_hue(panel);
  } else {
    panels[panel].colors[0] = 0; panels[panel].colors[1] = 0; panels[panel].colors[2] = 0;
  }
}

void assignAddress() {
  for (uint8_t address = 10; address < maxpanelsCount + 10; address++ )  {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error != 0) {
      Wire.beginTransmission(0x7E);
      Wire.write(address);
      Wire.endTransmission();
      return;
    }
  }
}

bool check_pannel_online (uint8_t panelId) {
  Wire.beginTransmission(panelId + 10);
  error = Wire.endTransmission();
  if (error == 0) {
    panels[panelId].reachable = true;
    return true;
  } else {
    panels[panelId].reachable = false;
    return false;
  }
}

bool check_temporary_address() {
  Wire.beginTransmission(0x7E);
  error = Wire.endTransmission();
  if (error == 0) {
    assignAddress();
    blinkLed (1, green, 500);
    return true;
  } else {
    blinkLed (1, red, 500);
    return false;
  }
}


void detection() {
  check_temporary_address();
  for (uint8_t panel = 0; panel < maxpanelsCount; panel++) {
    check_pannel_online(panel);
    if (panels[panel].reachable == true) {
      blinkLed (1, magenta, 500);
      byte i2cMessage[2] = {252, 250};
      Wire.beginTransmission(panel + 10);
      Wire.write(i2cMessage, 2);
      Wire.endTransmission();
      delay(200);
      check_temporary_address();
      i2cMessage[1] = 251;
      Wire.beginTransmission(panel + 10);
      Wire.write(i2cMessage, 2);
      Wire.endTransmission();
      delay(200);
      check_temporary_address();
    }
  }
}

void setup()  {
  pinMode(12, INPUT);
  pinMode(13, INPUT);
  strip.Begin();
  //strip.Show();
  //Serial.begin(115200);
  delay(100);
  strip.SetPixelColor(0, red);
  strip.Show();
  Wire.begin(5, 4);
  delay(100);

  for (uint8_t address = 0; address <  120; address++ )  {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      //Serial.print((String)address + ", ");
    }
  }

#ifdef USE_STATIC_IP
  WiFi.config(strip_ip, gateway_ip, subnet_mask);
#endif

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect(light_name);

  WiFi.macAddress(mac);

  blinkLed(2, green);

  httpUpdateServer.setup(&server);

  Udp.begin(2100);

  server.on("/config", HTTP_POST, []() {


    server.send ( 200, "text/json", server.arg("plain") );
  });

    server.on("/state", HTTP_PUT, []() {
    bool stateSave = false;
    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, server.arg("plain"));

    if (error) {
      server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
    } else {
      for (JsonPair state : root.as<JsonObject>()) {
        const char* key = state.key().c_str();
        int light = atoi(key) - 1;
        JsonObject values = state.value();
        int transitiontime = 4;

        if (values.containsKey("xy")) {
          panels[light].x = values["xy"][0];
          panels[light].y = values["xy"][1];
          panels[light].colorMode = 1;
        } else if (values.containsKey("ct")) {
          panels[light].ct = values["ct"];
          panels[light].colorMode = 2;
        } else {
          if (values.containsKey("hue")) {
            panels[light].hue = values["hue"];
            panels[light].colorMode = 3;
          }
          if (values.containsKey("sat")) {
            panels[light].sat = values["sat"];
            panels[light].colorMode = 3;
          }
        }

        if (values.containsKey("on")) {
          if (values["on"]) {
            panels[light].lightState = true;
          } else {
            panels[light].lightState = false;
          }
        }

        if (values.containsKey("bri")) {
          panels[light].bri = values["bri"];
        }

        if (values.containsKey("bri_inc")) {
          panels[light].bri += (int) values["bri_inc"];
          if (panels[light].bri > 255) panels[light].bri = 255;
          else if (panels[light].bri < 1) panels[light].bri = 1;
        }

        if (values.containsKey("transitiontime")) {
          transitiontime = values["transitiontime"];
        }

        processLightdata(light);
        byte payload[4];
        payload[0] = char(panels[light].colors[0]); payload[1] = char(panels[light].colors[1]); payload[2] = char(panels[light].colors[2]); payload[3] = transitiontime;
        Wire.beginTransmission(light + 10);
        Wire.write(payload, 4);
        Wire.endTransmission();
      }
      
      String output;
      serializeJson(root, output);
      server.send(200, "text/plain", output);
    }
  });

  server.on("/i2c", []() {
    String output;
    for (uint8_t address = 0; address <  127; address++ )  {
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      if (error == 0) {
        output += (String)address + ", ";
      }
    }
    server.send(200, "text/plain", "i2c scan result " + output);
  });


  server.on("/state", HTTP_GET, []() {

    uint8_t light;
    if (server.hasArg("light"))
      light = server.arg("light").toInt() - 1;

    DynamicJsonDocument root(1024);
    root["lightState"] = panels[light].lightState;
    root["bri"] = panels[light].bri;
    JsonArray xy = root.createNestedArray("xy");
    xy.add(panels[light].x);
    xy.add(panels[light].y);
    root["ct"] = panels[light].ct;
    root["hue"] = panels[light].hue;
    root["sat"] = panels[light].sat;
    if (panels[light].colorMode == 1)
      root["colormode"] = "xy";
    else if (panels[light].colorMode == 2)
      root["colormode"] = "ct";
    else if (panels[light].colorMode == 3)
      root["colormode"] = "hs";
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/detect", []() {
    uint8_t detectedLights = 0;
    for (uint8_t address = 10; address < maxpanelsCount + 10; address++ )  {
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      if (error == 0) {
        detectedLights++;
      }
    }
    char macString[32] = {0};
    sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    DynamicJsonDocument root(1024);
    root["name"] = light_name;
    root["hue"] = "bulb";
    root["protocol"] = "native_multi";
    root["lights"] = detectedLights;
    root["modelid"] = "LCT015";
    root["type"] = "json";
    root["mac"] = String(macString);
    root["version"] = 2.0;
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/reset", []() {
    server.send(200, "text/plain", "perform reset");
    delay(100);
    blinkLed(2, yellow);
    ESP.reset();
  });

  server.on("/scan", []() {
    server.send(200, "text/plain", "scanning...");
    blinkLed(1, yellow);
    detection();
  });

  server.onNotFound(handleNotFound);

  server.begin();
}

void entertainment() {
  uint8_t packetSize = Udp.parsePacket();
  if (packetSize) {
    Udp.read(packetBuffer, packetSize);
    for (uint8_t i = 0; i < packetSize / 4; i++) {
      panels[packetBuffer[i * 4]].colors[0] = packetBuffer[i * 4 + 1];
      panels[packetBuffer[i * 4]].colors[1] = packetBuffer[i * 4 + 2];
      panels[packetBuffer[i * 4]].colors[2] = packetBuffer[i * 4 + 3];
      byte rgb[3] = {panels[packetBuffer[i * 4]].colors[0], panels[packetBuffer[i * 4]].colors[1], panels[packetBuffer[i * 4]].colors[2]};
      Wire.beginTransmission((packetBuffer[i * 4] + 10));
      Wire.write(rgb, 3);
      Wire.endTransmission();
      delay(2);
    }
  }
}


void loop()
{
  //lightEngine();
  server.handleClient();
  entertainment();
  if (digitalRead(12) == HIGH) {
    blinkLed(2, yellow);
    delay(100);
    ESP.reset();
  }
  if (digitalRead(13) == HIGH) {
    blinkLed(1, yellow);
    detection();
  }
}
