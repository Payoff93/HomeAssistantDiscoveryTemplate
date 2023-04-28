#include <ESP8266WiFi.h>  // Wifi connection
#include <PubSubClient.h>  // MQTT client http://knolleary.net
#include <ArduinoJson.h>  // For MQTT: JSON generator https://arduinojson.org
#include <DNSServer.h>  // DNS Server for connected client
#include <ESP8266WebServer.h>  // HTTP Server by Ivan Grokhotkov
#include <WiFiManager.h>  // Wifi Access Point https://github.com/tzapu

// ---- Configuration ----
// Connectivity
const char*         kWifiSSID = "SSID";
const char*         kWifiPass = "WiFiPass";
const char*         kMQTTServer = "192.168.0.14";
const char*         kMQTTUser = "user";
const char*         kMQTTPass = "pass";
const int           kMQTTPort = 1883;
const size_t        kMQTTBufferSize = 600; // Don't set to low, otherwise mqtt-msgs may be ignored
// Device Info
const char*         kDeviceModel = "ESP8266";  // Hardware Model
const char*         kSWVersion = "1.0";  // Firmware Version
const char*         kManufacturer = "My name";  // Manufacturer Name
const String        kDeviceName = "DeviceName";  // Device Name
const String        kMQTTStatusTopic = "esp8266sensor/" + kDeviceName;  // MQTT Topic

// ---- Configuration END ----

#define PERIOD_MILLSEC_1000    1000
#define PERIOD_MILLSEC_500     500
#define PERIOD_MILLSEC_250     250

WiFiClient          _wifi_client;
PubSubClient        _mqtt_pub_sub(_wifi_client);
WiFiServer          _web_server(80);

int                 _mqtt_counter_conn = 0;
uint32_t            _time = 0;
bool                _init_system = true;
bool                _send_mqtt_data = false;
String              _mac_id;
String              _header;

void MQTTHomeAssistantDiscovery() {
    String discovery_topic;
    String payload;
    String str_payload;
    if (_mqtt_pub_sub.connected()) {
        Serial.println("Sending mqtt message for Home Assistant auto discovery feature");
        StaticJsonDocument<kMQTTBufferSize> payload;
        JsonObject device;
        JsonArray identifiers;

        // Define Entities here
        // ---- Temperature ----
        discovery_topic = "homeassistant/sensor/" + kMQTTStatusTopic + "_temp" + "/config";

        payload["name"] = kDeviceName + ".temp";
        payload["uniq_id"] = _mac_id + "_temp";
        payload["stat_t"] = kMQTTStatusTopic;
        payload["dev_cla"] = "temperature";
        payload["val_tpl"] = "{{ value_json.temp | is_defined }}";
        payload["unit_of_meas"] = "°C";
        device = payload.createNestedObject("device");
        device["name"] = kDeviceName;
        device["model"] = kDeviceModel;
        device["sw_version"] = kSWVersion;
        device["manufacturer"] = kManufacturer;
        identifiers = device.createNestedArray("identifiers");
        identifiers.add(_mac_id);
        // generate JSON from string
        serializeJsonPretty(payload, Serial);
        Serial.println(" ");
        serializeJson(payload, str_payload);
        // Send message to MQTT
        _mqtt_pub_sub.publish(discovery_topic.c_str(), str_payload.c_str());

        // Prepare storage for new discovery entities
        payload.clear();
        device.clear();
        identifiers.clear();
        str_payload.clear();

        // add other entities like above ...
    }
}


void MQTTReceiverCallback(char* topic, byte* inFrame, unsigned int length) {
    Serial.print("Message arrived on topic: ");
    Serial.print(topic);
    Serial.print(". Message: ");
    String message_temp;

    // Generate message string
    for (unsigned int i = 0; i < length; i++) {
        Serial.print(static_cast<char>(inFrame[i]));
        message_temp += static_cast<char>(inFrame[i]);
    }
    Serial.println();

    if (String(topic) == String("homeassistant/status")) {
        if (message_temp == "online")
            MQTTHomeAssistantDiscovery();
    }
}

void MQTTReconnect() {
    // Loop until we're reconnected
    while (!_mqtt_pub_sub.connected()  && (_mqtt_counter_conn++ < 4)) {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (_mqtt_pub_sub.connect(kDeviceName.c_str(), kMQTTUser, kMQTTPass)) {
            Serial.println("connected");
            // Subscribe
            _mqtt_pub_sub.subscribe("homeassistant/status");
            delay(PERIOD_MILLSEC_250);
        } else {
            Serial.print("failed, rc=");
            Serial.print(_mqtt_pub_sub.state());
            Serial.println(" try again in 1 seconds");
            delay(PERIOD_MILLSEC_1000);
        }
    }
    _mqtt_counter_conn = 0;
}

/** \brief Connect to WiFi and generate the unique id from MAC */
void WiFiSetup() {
    int counter = 0;
    byte mac[6];

    delay(PERIOD_MILLSEC_250);

    // Connect to Wifi network
    Serial.print("Connecting to ");
    Serial.println(kWifiSSID);

    WiFi.begin(kWifiSSID, kWifiPass);

    WiFi.macAddress(mac);
    _mac_id =  String(mac[0], HEX) + String(mac[1], HEX)
     + String(mac[2], HEX) + String(mac[3], HEX) + String(mac[4], HEX) + String(mac[5], HEX);

    Serial.print("Unique ID: ");
    Serial.println(_mac_id);

    while (WiFi.status() != WL_CONNECTED && counter++ < 8) {
        delay(PERIOD_MILLSEC_1000);
        Serial.print(".");
    }
    Serial.println("");

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi is connected");
        Serial.print("Optained IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("WiFi connection failed!");
    }
}

void HTTPHandler() {
    // Listen for incoming clients
    WiFiClient client = _web_server.available();
    // If a new client connects,
    if (client) {
        Serial.println("New HTTP connection");          // print a message out in the serial port
        String currentLine = "";                // make a String to hold incoming data from the client
        while (client.connected()) {            // loop while the client's connected
            if (client.available()) {             // if there's bytes to read from the client,
                char c = client.read();             // read a byte, then
                Serial.write(c);                    // print it out the serial monitor
                _header += c;
                if (c == '\n') {                    // if the byte is a newline character
                    // if the current line is blank, you got two newline characters in a row.
                    // that's the end of the client HTTP request, so send a response:
                    if (currentLine.length() == 0) {
                        // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                        // and a content-type so the client knows what's coming, then a blank line:
                        client.println("HTTP/1.1 200 OK");
                        client.println("Content-type:text/html");
                        client.println("Connection: close");
                        client.println();

                        // Display the HTML web page
                        client.println("<!DOCTYPE html><html>");
                        client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                        client.println("<link rel=\"icon\" href=\"data:,\">");
                        // CSS to style the on/off buttons
                        client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
                        client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
                        client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
                        client.println(".button2 {background-color: #77878A;}</style></head>");

                        // Web Page Heading
                        client.println("<body><h1>ESP8266 Web Server</h1>");
                        // If the output5State is off, it displays the ON button
                        client.println("<p><a href=\"/5/off\"><button class=\"button button2\">OFF</button></a></p>");
                        client.println("</body></html>");

                        // The HTTP response ends with another blank line
                        client.println();
                        break;
                    } else {  // if you got a newline, then clear currentLine
                        currentLine = "";
                    }
                } else if (c != '\r') {  // if you got anything else but a carriage return character,
                    currentLine += c;      // add it to the end of the currentLine
                }
            }
        }
        // Clear the header variable
        _header = "";
        // Close the connection
        client.stop();
        Serial.println("Client disconnected.");
        Serial.println("");
    }
}

/** \brief Mandatory setup routine */
void setup() {
    // Input and output definitions
    Serial.begin(115200);
    delay(PERIOD_MILLSEC_500);

    Serial.println("");
    Serial.println("");
    Serial.println("----------------------------------------------");
    Serial.print("MODEL: ");
    Serial.println(kDeviceModel);
    Serial.print("DEVICE: ");
    Serial.println(kDeviceName);
    Serial.print("SW Rev: ");
    Serial.println(kSWVersion);
    Serial.println("----------------------------------------------");

    // Init network connection
    WiFiSetup();
    // Wifi Manager initialization and setup
    WiFiManager wifi_manager;
    // Define the name of the access point in case no wifi credentials are supplied
    wifi_manager.autoConnect((String(kDeviceName) + String("AP")).c_str());

    // MQTT initialization
    _mqtt_pub_sub.setServer(kMQTTServer, kMQTTPort);
    _mqtt_pub_sub.setCallback(MQTTReceiverCallback);
    // Very important, if buffer is on default value the mqtt message will not be send
    _mqtt_pub_sub.setBufferSize(kMQTTBufferSize);

    Serial.println("Connected.");

    _web_server.begin();
}

/** \brief Mandatory loop routine */
void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!_mqtt_pub_sub.connected()) {
            MQTTReconnect();
        } else {
            _mqtt_pub_sub.loop();
        }
    }

    if (_init_system) {
        delay(PERIOD_MILLSEC_250);
        _init_system = false;
        Serial.println("Initializing system ...");
         // Send Discovery Data
        MQTTHomeAssistantDiscovery();
    }
    // Do stuff every 500ms ... like reading a sensor
    if (millis() - _time > PERIOD_MILLSEC_1000) {
        _time = millis();

        // Some sensor event happened (e.g.)
        _send_mqtt_data = true;
    }

    // Update MQTT entities
    if (_send_mqtt_data) {
        StaticJsonDocument<200> payload;
        payload["temp"] = random(30);  // dummy

        String str_payload;
        serializeJson(payload, str_payload);

        if (_mqtt_pub_sub.connected()) {
            _mqtt_pub_sub.publish(kMQTTStatusTopic.c_str(), str_payload.c_str());
            Serial.println("MQTT updated");
            _send_mqtt_data = false;
        }
    }


    HTTPHandler();
}