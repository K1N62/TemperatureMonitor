#include "Wire.h"
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>
#include <PubSubClient.h>

#define I2C_SCL D1
#define I2C_SDA D2
#define PinDHT D4
#define PinBtn D5
#define PinBat A0
#define CHAR_W 6
#define CHAR_H 10

// SSID and password for the wifi network
#define ssid "xxxxxxxxx"
#define password "xxxxxxxxx"
// MQTT Broker details
#define mqtt_server "10.0.0.1"
#define mqtt_port 1883
#define mqtt_user "xxxxxxxxx"
#define mqtt_password "xxxxxxxxx"

// MQTT topic configuration
#define client_uuid4 "f3364ef5-aa08-47ff-8a73-d2155eb60a02"
#define battery_topic "home/sensor/room/battery"
#define humidity_topic "home/sensor/room/humidity"
#define temp_topic "home/sensor/room/temperature"
#define status_topic "home/sensor/room/status"
#define will_topic "home/sensor/room/status"
#define will_message "disconnected"
#define config_topic "home/sensor/f3364ef5-aa08-47ff-8a73-d2155eb60a02/config"

// Uncomment this to enable mqtt autodetection topics
// #define USE_AUTODETECT

#ifdef USE_AUTODETECT
const char *config_battery = "{\"device_class\":\"voltage\","
                             "\"unique_id\":\"f3364ef5-aa08-47ff-8a73-d2155eb60a02-battery\","
                             "\"name\":\"Voltage\","
                             "\"state_topic\":\"home/sensor/room/battery\","
                             "\"unit_of_measurement\":\"v\","
                             "\"availability\":{\"topic\":\"home/sensor/room/status\",\"payload_availible\":\"online\",\"payload_not_available\":\"offline\"}}";

const char *config_humidity = "{\"device_class\":\"humidity\","
                              "\"unique_id\":\"f3364ef5-aa08-47ff-8a73-d2155eb60a02-humidity\","
                              "\"name\":\"Humidity\","
                              "\"state_topic\":\"home/sensor/room/humidity\","
                              "\"unit_of_measurement\":\"%\","
                              "\"availability\":{\"topic\":\"home/sensor/room/status\",\"payload_availible\":\"online\",\"payload_not_available\":\"offline\"}}";

const char *config_temp = "{\"device_class\":\"temperature\","
                          "\"unique_id\":\"f3364ef5-aa08-47ff-8a73-d2155eb60a02-temperature\","
                          "\"name\":\"Temperature\","
                          "\"state_topic\":\"home/sensor/room/temperature\","
                          "\"unit_of_measurement\":\"°C\","
                          "\"availability\":{\"topic\":\"home/sensor/room/status\",\"payload_availible\":\"online\",\"payload_not_available\":\"offline\"}}";
#endif

// Init the DHT
DHT dht(PinDHT, DHT22);
// Init the OLED display
U8G2_SSD1306_128X64_NONAME_2_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);

// Init the tcp server, uncomment this to enable raw tcp connections
//#define TCP_SERVER
//WiFiServer server(9000);

// Init the wifi client
WiFiClient espClient;
// Init the MQTT client
PubSubClient mqttClient(espClient);

// Init variables
float t, h, v, pt, ph = 0.0;
uint8_t row_offset, con_counter = 0;
unsigned long last_dht_update = millis();
volatile unsigned long t_awake = millis();
volatile bool awake = true;

void u8g2_prepare()
{
  u8g2.setFont(u8g2_font_6x10_mr);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setFontMode(1);
  u8g2.setDrawColor(2);
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);
  row_offset = 0;
}

// Helper function to print text
void writeText(uint8_t x, uint8_t row, String text)
{
  u8g2.drawStr(x, row * CHAR_H + row_offset, text.c_str());
}

// Helper function to print UTF-8 charactesr
void writeUtf8(uint8_t x, uint8_t row, String text)
{
  u8g2.setFont(u8g2_font_unifont_t_symbols);
  char buf[129] = {0};
  text.toCharArray(buf, 128);
  u8g2.drawUTF8(x, row * (CHAR_H - 1) + row_offset, buf);
  u8g2.setFont(u8g2_font_6x10_mr);
}

// Helper function to clear the screen buffer
void clearScreen()
{
  u8g2.clearBuffer();
  u8g2_prepare();
}

// Helper function to center the text by calculating the left margin
// len: int, the number of characters in the string to center
int centerText(int len)
{
  return max(0, (int)((128 - len * CHAR_W) / 2));
}

// Trigger the buffer write to the screen
void drawScreen()
{
  u8g2.setPowerSave(!awake);
  clearScreen();
  u8g2.firstPage();
  do
  {
    drawStatusBar();
    drawTemp();
    drawWifi();
  } while (u8g2.nextPage());
}

// Print the temperature and humidity
void drawTemp()
{
  String st = String("    Temp: ") + t;
  String sh = String("Humidity: ") + h + " %";
  writeText(0, 2, st);
  writeUtf8(st.length() * CHAR_W - 1, 2, String("°"));
  writeText((st.length() + 1) * CHAR_W, 2, "c");
  writeText(0, 3, sh);
}

// Print the wifi status
void drawWifi()
{
  writeText(0, 5, "IP: " + WiFi.localIP().toString());
  switch (WiFi.status())
  {
  case WL_CONNECTED:
    writeText(128 - 2 * CHAR_W, 5, "OK");
    break;
  case WL_NO_SSID_AVAIL:
    break;
  case WL_CONNECT_FAILED:
    break;
  case WL_IDLE_STATUS:
    writeText(128 - 4 * CHAR_W, 5, "IDLE");
    break;
  case WL_DISCONNECTED:
    writeText(128 - 2 * CHAR_W, 5, "DC");
    break;
  }
}

// Print the wifi status bar
void drawStatusBar()
{
  String rssi = WiFi.RSSI() + String("dBm");
  writeText(0, 0, String("Bat: ") + v);
  writeText(128 - rssi.length() * CHAR_W, 0, rssi);
}

// Print the welcome text, simple splash screen stating the app name
void printWelcome()
{
  clearScreen();
  u8g2.firstPage();
  do
  {
    writeText(centerText(19), 3, "Temperature Monitor");
  } while (u8g2.nextPage());
  delay(1000);
}

// Print the connecting status sequence. First wait for wifi to connect
// then try connect to the MQTT broker.
void drawConnecting()
{
  char loading;
  switch (con_counter % 3)
  {
  case 0:
    loading = '/';
    break;
  case 1:
    loading = '-';
    break;
  case 2:
    loading = '\\';
    break;
  }

  clearScreen();
  u8g2.firstPage();
  do
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      writeText(0, 1, "- Connecting to Wifi");
    }
    else
    {
      writeText(0, 1, "+ Connected to Wifi");
    }

    if (!mqttClient.connected())
    {
      writeText(0, 2, "- Connecting to Broker");
    }
    else
    {
      writeText(0, 2, "+ Connected to Broker");
    }

    writeText(centerText(9), 4, String("Working ") + loading);
  } while (u8g2.nextPage());

  con_counter++;
}

// Update the global variables from the digital humidity and temperature module
void updateDHT()
{
  t = dht.readTemperature();
  h = dht.readHumidity();

  // Dont allow value of nan...
  if (isnan(t))
  {
    t = 0;
  }
  if (isnan(h))
  {
    h = 0;
  }
}

// The TCP handler is simple but could potentially cause issues as it (in the
// current implementation) blocks the main loop if the client don't respond
#if TCP_SERVER
void handleTCP()
{
  espClient = server.available();
  if (espClient)
  {
    // Client connected
    bool commandDone = false;
    String command = "";

    // Get data until command is done and client is connected
    while (espClient.connected() && !commandDone)
    {
      // Read the incoming TCP command
      while (espClient.available() > 0)
      {
        char c = espClient.read();
        if (c >= 'a' && c <= 'z' || c == '\n')
        {
          if (c == '\n' || command.length() > 64)
          {
            commandDone = true;
            break;
          }
          command += c;
        }
      }
    }

    String r;
    if (strcmp(command.c_str(), "hello") == 0)
    {
      // Health check, simple ping pong
      espClient.write("hello");
    }
    else if (strcmp(command.c_str(), "read") == 0)
    {
      // Return the current state and readings
      r = String(v) + "," + t + "," + h;
      espClient.write(r.c_str());
    }
    else
    {
      espClient.write("invalid command");
    }

    espClient.stop();
  }
}
#endif

// Handle the connection to the MQTT broker and print the status
void mqttConnect()
{
  // If the connection is not open try open it
  while (!mqttClient.connected())
  {
    // Print the status
    drawConnecting();
    // Try connecting and set the will topic + message
    if (mqttClient.connect(client_uuid4, mqtt_user, mqtt_password, will_topic, 1, true, will_message))
    {
      // If success publish status online to the status topic
      if (mqttClient.publish(status_topic, "online", false))
      {
        Serial.println("Sensor online!");
      }

#ifdef USE_AUTODETECT
      // If enabled, publish the MQTT auto config topics
      mqttClient.publish(config_topic, config_battery, true);
      mqttClient.publish(config_topic, config_temp, true);
      mqttClient.publish(config_topic, config_humidity, true);
#endif

      return;
    }

    // Debug
    Serial.print("failed, rc=");
    Serial.println(mqttClient.state());
    // Wait some time before trying again
    delay(250);
  }
}

// Handle the MQTT loop and update topics
void handleMQTT()
{
  if (!mqttClient.connected())
  {
    mqttConnect();
  }
  mqttClient.loop();

  // Check if values have changed from previous update
  // Threshold is .25 degrees or .25% humidity from previous value
  if (abs(pt - t) >= .25)
  {
    pt = t;
    if (mqttClient.publish(temp_topic, String(t).c_str(), true))
    {
      Serial.println("Published temperature topic");
    }
  }
  if (abs(ph - h) >= .25)
  {
    ph = h;
    if (mqttClient.publish(humidity_topic, String(h).c_str(), true))
    {
      Serial.println("Published humidity topic");
    }
  }
}

// Interupt handler when pushing the wakeup button
ICACHE_RAM_ATTR void handleButton()
{
  //WiFi.forceSleepWake();  // Not used atm
  // This is not ideal as calling millis in the interupt handler causes
  // longer delays from the loop
  t_awake = millis();
  awake = true;
}

// Setup
void setup()
{
  // Start the I2C clock
  Wire.begin();
  Wire.setClock(100000);

  // Start the screen module
  u8g2.begin();

  // Start the DHT module
  dht.begin();

  // Setup the interupt
  attachInterrupt(digitalPinToInterrupt(PinBtn), handleButton, RISING);

  // Setup the serial interface
  Serial.begin(115200);

  // Gimmick
  printWelcome();

  // Setup the WiFi in station mode only
  wifi_set_sleep_type(LIGHT_SLEEP_T);
  WiFi.persistent(false);
  WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for the Wi-Fi to connect
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(250);
    drawConnecting();
  }

  // Start the TCP server (if enabled)
#if TCP_SERVER
  server.begin();
#endif

  // Init the MQTT server connection
  mqttClient.setServer(mqtt_server, mqtt_port);

  t_awake = millis();
  updateDHT();
}

// Main loop
void loop()
{
  // Check battery
  v = analogRead(PinBat) / 1023.0 * 4.55;

  // If we have a connected battery and it's low on power, shut down
  if (v > .2 && v < 3.35)
  {
    ESP.deepSleep(0);
  }

  // Update readings only each 5 seconds
  if (abs(millis() - last_dht_update) > 5000)
  {
    last_dht_update = millis();
    // Get new readings
    updateDHT();

    // Also update the battery topic if it's connected
    if (v > .2 && mqttClient.publish(battery_topic, String(v).c_str(), true))
    {
      Serial.println("Published battery value");
    }
  }

  // Blocking polling, not very practical
#ifdef TCP_SERVER
  handleTCP();
#endif

  // Start mqtt
  handleMQTT();

  if (awake)
  {
    drawScreen();

    // If the screen has been on for 15 seconds, turn it off
    if (abs(millis() - t_awake) > 15000)
    {
      awake = false;
      u8g2.setPowerSave(!awake);
      //WiFi.forceSleepBegin();
    }
  }

  delay(100);
}
