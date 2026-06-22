#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Arduino_JSON.h>
#include <esp_task_wdt.h> // Native library for the Watchdog
#include <WiFiManager.h>  // Library for the WiFi captive portal
#include <Preferences.h>  // Library to persist MQTT settings in flash

// Watchdog configuration
#define WDT_TIMEOUT 8 // Grace period in seconds before a forced reboot

// Feature configuration
const bool wifi_enable = true;
const bool mqtt_enable = true;
const bool ota_enable = true;

// --- ANSI COLORS FOR THE TERMINAL ---
#define C_RESET   "\033[0m"
#define C_RED     "\033[1;31m"
#define C_GREEN   "\033[1;32m"
#define C_YELLOW  "\033[1;33m"
#define C_CYAN    "\033[1;36m"
#define C_MAGENTA "\033[1;35m"

// Timers and intervals
unsigned long previousHeartbeat = 0;
unsigned long previousSensorRead = 0;
unsigned long heartbeat;
const unsigned long sensor_read_interval = 500;
const unsigned long mqtt_update_interval = 20000;

// Heating cycle parameters (anti-short-cycle)
unsigned long lastHeatingStateChange = 0;
const unsigned long minHeatingCycle = 15 * 60 * 1000; // 15 minutes

// Inrush current limiting: startup stagger between the two heating elements
unsigned long heater1OnTime = 0;
const unsigned long heaterStageDelay = 20000; // 20s between heater 1 and 2 (per the original board)
bool isFirstCycle = true;

// Variables for the non-blocking reset
bool resetRequested = false;
unsigned long resetTimer = 0;
const unsigned long resetDelay = 10000;

// ==========================================
// DYNAMIC CONFIGURATION (WIFIMANAGER)
// ==========================================
Preferences preferences;

char mqttServer[40] = "";
char mqttPortStr[6] = "1883";
int  mqttPort = 1883;
char mqttUser[32] = "";
char mqttPass[32] = "";
char otaPass[32] = ""; // password for OTA updates (empty = OTA unprotected)

bool shouldSaveConfig = false;

void saveConfigCallback() {
  Serial.println("Configuration change detected. Save requested.");
  shouldSaveConfig = true;
}

void loadConfig() {
  preferences.begin("spa_config", false);
  String s_server = preferences.getString("mqtt_server", "");
  String s_port   = preferences.getString("mqtt_port", "1883");
  String s_user   = preferences.getString("mqtt_user", "");
  String s_pass   = preferences.getString("mqtt_pass", "");
  String s_ota    = preferences.getString("ota_pass", "");

  strcpy(mqttServer, s_server.c_str());
  strcpy(mqttPortStr, s_port.c_str());
  strcpy(mqttUser, s_user.c_str());
  strcpy(mqttPass, s_pass.c_str());
  strcpy(otaPass, s_ota.c_str());

  mqttPort = atoi(mqttPortStr);
  preferences.end();
}

void saveConfig() {
  preferences.begin("spa_config", false);
  preferences.putString("mqtt_server", mqttServer);
  preferences.putString("mqtt_port", mqttPortStr);
  preferences.putString("mqtt_user", mqttUser);
  preferences.putString("mqtt_pass", mqttPass);
  preferences.putString("ota_pass", otaPass);
  preferences.end();
  shouldSaveConfig = false;
}

// ==========================================
// STATE VARIABLES
// ==========================================
bool request_mqtt_update = false;

// Telnet server for OTA debug
WiFiServer telnetServer(23);
WiFiClient telnetClient;

// MQTT topic list
const char* mqttTopicJet = "spa_intex/jet";
const char* mqttTopicFiltration = "spa_intex/filtration";
const char* mqttTopicTarget = "spa_intex/target";
const char* mqttTopicTempRegulation = "spa_intex/thermostat";
const char* mqttTopicInfo = "spa_intex/info";
const char* mqttTopicReset = "spa_intex/reset";
const char* mqttTopicError = "spa_intex/error";
const char* mqttTopicBypassFlow = "spa_intex/bypass_flow";
const char* mqttTopicBypassFuse = "spa_intex/bypass_fuse";

// Pin configuration
#define HEATER_1_PIN 18
#define HEATER_2_PIN 19
#define JET_PIN 5
#define PUMP_PIN 17
#define FLOW_1_PIN 23
#define FLOW_2_PIN 22
#define TEMP_1_PIN 34
#define TEMP_2_PIN 35
#define TEMP_FUSE_PIN 32

// Logic level of the safety inputs (verify against the board wiring)
#define FLOW_ACTIVE_LEVEL HIGH   // level read when water flow is present
#define FUSE_OK_LEVEL HIGH       // level read when the thermal fuse is intact
#define SERIESRESISTOR_TEMP_1 9980
#define SERIESRESISTOR_TEMP_2 9980

const int num_samples = 20;
const float smoothing_factor = 0.2;
bool first_reading = true;

WiFiClient espClient;
PubSubClient MQTTclient(espClient);

float target_temp = 35.0;
float current_temp = 0.0;
float temp_1 = 0.0;
float temp_2 = 0.0;
bool flow_1 = false;
bool flow_2 = false;
bool fuse = false;
bool offline_mode = false;
bool filtration_enabled = false;
bool jet_enabled = false;
bool temp_regulation_enabled = false;
bool heating_enabled = false;
bool bypass_flow = false; // override of the "water flow" interlock (reset to 0 on boot)
bool bypass_fuse = false; // override of the "thermal fuse" interlock (reset to 0 on boot)

float hysterisys = 1.0;
int max_temp = 39;

// ==========================================
// OTA DEBUG FUNCTIONS
// ==========================================
template <typename T>
void debugPrint(T msg) {
  Serial.print(msg);
  if (telnetClient && telnetClient.connected()) {
    telnetClient.print(msg);
  }
}

template <typename T>
void debugPrintln(T msg) {
  Serial.println(msg);
  if (telnetClient && telnetClient.connected()) {
    telnetClient.println(msg);
  }
}

void debugPrintln() {
  Serial.println();
  if (telnetClient && telnetClient.connected()) {
    telnetClient.println();
  }
}

void handleTelnet() {
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) telnetClient.stop();
      telnetClient = telnetServer.available();
      // Clear the screen on connect (ANSI Clear Screen)
      telnetClient.print("\033[2J\033[H");
      telnetClient.println(String(C_GREEN) + "=== OTA DEBUG CONNECTION ESTABLISHED ===" + C_RESET);
    } else {
      telnetServer.available().stop();
    }
  }
}

// Write a colored state label into buf (no heap allocation, unlike a String return).
void fmtState(char* buf, size_t n, bool state, const char* tTrue, const char* tFalse) {
  snprintf(buf, n, "%s%s%s", state ? C_GREEN : C_RED, state ? tTrue : tFalse, C_RESET);
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(HEATER_1_PIN, OUTPUT);
  pinMode(HEATER_2_PIN, OUTPUT);
  pinMode(JET_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);

  pinMode(TEMP_1_PIN, INPUT);
  pinMode(TEMP_2_PIN, INPUT);
  pinMode(FLOW_1_PIN, INPUT);
  pinMode(FLOW_2_PIN, INPUT);
  pinMode(TEMP_FUSE_PIN, INPUT);

  // --- WIFIMANAGER ---
  loadConfig();

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);

  pinMode(0, INPUT_PULLUP); // GPIO 0 = ESP32 BOOT button
  if (digitalRead(0) == LOW) {
    Serial.println("BOOT button held: erasing WiFi settings!");
    wm.resetSettings();
  }

  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqttServer, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqttPortStr, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqttUser, 32);
  WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", mqttPass, 32, "type='password'");
  WiFiManagerParameter custom_ota_pass("otapass", "OTA Password", otaPass, 32, "type='password'");

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_ota_pass);

  wm.setConfigPortalTimeout(180);

  Serial.println("Attempting WiFi connection...");

  if (!wm.autoConnect("Spa-Intex-Config")) {
    Serial.println("WiFi connection failed -> Offline mode enabled");
    offline_mode = true;
  } else {
    Serial.println("WiFi connected!");
    offline_mode = false;

    strcpy(mqttServer, custom_mqtt_server.getValue());
    strcpy(mqttPortStr, custom_mqtt_port.getValue());
    strcpy(mqttUser, custom_mqtt_user.getValue());
    strcpy(mqttPass, custom_mqtt_pass.getValue());
    strcpy(otaPass, custom_ota_pass.getValue());
    mqttPort = atoi(mqttPortStr);

    if (shouldSaveConfig) {
      saveConfig();
      Serial.println("New MQTT configuration saved.");
    }
  }

  if(!offline_mode) {
    telnetServer.begin();
    if(mqtt_enable) connect_mqtt();
    if(ota_enable) activateOTA();
  }

  test_relay();

  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  debugPrintln(String(C_GREEN) + "Watchdog enabled successfully!" + C_RESET);
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  heartbeat = millis();

  handleTelnet();

  if (resetRequested && (heartbeat - resetTimer >= resetDelay)) {
    debugPrintln(String(C_YELLOW) + "Rebooting the ESP32..." + C_RESET);
    delay(500);
    ESP.restart();
  }

  if(ota_enable) {
    ArduinoOTA.handle();
  }

  if(mqtt_enable) {
    if (!MQTTclient.connected() && !offline_mode) {
      static unsigned long lastMqttRetry = 0;
      if (heartbeat - lastMqttRetry > 5000) {
        lastMqttRetry = heartbeat;
        connect_mqtt();
      }
    }
    MQTTclient.loop();
  }

  if (heartbeat - previousSensorRead >= sensor_read_interval) {
    previousSensorRead = heartbeat;
    read_sensors();
  }

  if(mqtt_enable && !offline_mode && MQTTclient.connected()) {
    if (heartbeat - previousHeartbeat >= mqtt_update_interval || request_mqtt_update) {
      previousHeartbeat = heartbeat;
      request_mqtt_update = false;
      update_mqtt_server();
    }
  }

  update_relay_state();

  static unsigned long lastDebugInfo = 0;
  if (heartbeat - lastDebugInfo > 5000) {
    lastDebugInfo = heartbeat;

    unsigned long elapsedSec = (heartbeat - lastHeatingStateChange) / 1000;
    unsigned long elMins = elapsedSec / 60;
    unsigned long elSecs = elapsedSec % 60;

    char line[200];
    char a[48], b[48];

    // Cycle lock state
    char lock[80];
    if (elapsedSec < (minHeatingCycle / 1000)) {
      unsigned long remSec = (minHeatingCycle / 1000) - elapsedSec;
      snprintf(lock, sizeof(lock), "%s%lum %lus remaining%s", C_YELLOW, remSec / 60, remSec % 60, C_RESET);
    } else {
      snprintf(lock, sizeof(lock), "%sUNLOCKED%s", C_GREEN, C_RESET);
    }
    if (isFirstCycle) strncat(lock, " (ignored for 1st cycle)", sizeof(lock) - strlen(lock) - 1);

    // Diagnostic (every branch is a compile-time constant colored literal)
    const char* diag = C_GREEN "Normal operation" C_RESET;
    if(temp_regulation_enabled && !heating_enabled) {
      if(!fuse && !bypass_fuse) diag = C_RED "BLOCKED: Thermal fuse tripped" C_RESET;
      else if((!flow_1 || !flow_2) && !bypass_flow) diag = C_RED "BLOCKED: No water flow" C_RESET;
      else if(current_temp >= max_temp) diag = C_RED "BLOCKED: 39C safety limit" C_RESET;
      else if(current_temp >= (target_temp - hysterisys) && current_temp <= (target_temp + hysterisys)) diag = C_YELLOW "Dead zone (hysteresis)" C_RESET;
      else if(current_temp > target_temp) diag = C_CYAN "Water at target temperature" C_RESET;
      else if(!isFirstCycle && elapsedSec < (minHeatingCycle / 1000)) diag = C_YELLOW "Waiting (anti-short-cycle)" C_RESET;
    }
    else if (temp_regulation_enabled && heating_enabled) {
      diag = C_GREEN "HEATING: Heating normally" C_RESET;
    }

    debugPrintln();
    debugPrintln(C_CYAN "===================================================" C_RESET);
    debugPrintln(C_CYAN "               SPA INTEX - DASHBOARD               " C_RESET);
    debugPrintln(C_CYAN "===================================================" C_RESET);

    snprintf(line, sizeof(line), "  Temperature  : %s%.1f C%s (Target: %s%.1f C%s)", C_YELLOW, current_temp, C_RESET, C_YELLOW, target_temp, C_RESET);
    debugPrintln(line);

    fmtState(a, sizeof(a), temp_regulation_enabled, "ACTIVE", "INACTIVE");
    snprintf(line, sizeof(line), "  Thermostat   : [%s]", a);
    debugPrintln(line);

    fmtState(a, sizeof(a), filtration_enabled, "ACTIVE", "INACTIVE");
    snprintf(line, sizeof(line), "  Filtration   : [%s]", a);
    debugPrintln(line);

    fmtState(a, sizeof(a), jet_enabled, "ON", "OFF");
    snprintf(line, sizeof(line), "  Bubbles (Jet): [%s]", a);
    debugPrintln(line);

    fmtState(a, sizeof(a), heating_enabled, "HEATING", "OFF");
    snprintf(line, sizeof(line), "  Heating      : [%s]", a);
    debugPrintln(line);

    fmtState(a, sizeof(a), flow_1, "DETECTED", "NO FLOW");
    fmtState(b, sizeof(b), flow_2, "DETECTED", "NO FLOW");
    snprintf(line, sizeof(line), "  Water Sensor : F1 [%s]  |  F2 [%s]", a, b);
    debugPrintln(line);

    fmtState(a, sizeof(a), !bypass_fuse, "ACTIVE", "BYPASSED");
    fmtState(b, sizeof(b), !bypass_flow, "ACTIVE", "BYPASSED");
    snprintf(line, sizeof(line), "  Interlocks   : Fuse [%s]  |  Flow [%s]", a, b);
    debugPrintln(line);

    debugPrintln(C_CYAN "---------------------------------------------------" C_RESET);

    snprintf(line, sizeof(line), "  Timer        : %lum %lus elapsed", elMins, elSecs);
    debugPrintln(line);

    snprintf(line, sizeof(line), "  Cycle lock   : %s", lock);
    debugPrintln(line);

    snprintf(line, sizeof(line), "  Diagnostic   : %s", diag);
    debugPrintln(line);

    debugPrintln(C_CYAN "===================================================" C_RESET);
  }

  esp_task_wdt_reset();
}

// ==========================================
// MQTT AUTO-DISCOVERY (HOME ASSISTANT)
// ==========================================
void publishMQTTDiscovery() {
  debugPrintln(String(C_CYAN) + "Sending MQTT auto-discovery..." + C_RESET);

  String deviceStr = "\"dev\":{\"ids\":[\"spa_intex_esp32\"],\"name\":\"Spa Intex\",\"mdl\":\"DIY ESP32\",\"mf\":\"Custom\"}";

  // 1. Temperature sensor
  String topicTemp = "homeassistant/sensor/spa_intex/temperature/config";
  String payloadTemp = "{\"name\":\"Spa Temperature\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ value_json.temp_2 }}\",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"uniq_id\":\"spa_temp\"," + deviceStr + "}";
  MQTTclient.publish(topicTemp.c_str(), payloadTemp.c_str(), true);

  // 2. Thermostat as a single climate entity (replaces the old number + switch below).
  //    Reuses the existing spa_intex/target and spa_intex/thermostat command topics.
  String topicClimate = "homeassistant/climate/spa_intex/thermostat/config";
  String payloadClimate = "{\"name\":\"Spa\",\"modes\":[\"off\",\"heat\"],\"mode_cmd_t\":\"spa_intex/thermostat\",\"mode_stat_t\":\"spa_intex/info\",\"mode_stat_tpl\":\"{{ 'heat' if value_json.thermostat else 'off' }}\",\"temp_cmd_t\":\"spa_intex/target\",\"temp_stat_t\":\"spa_intex/info\",\"temp_stat_tpl\":\"{{ value_json.target }}\",\"curr_temp_t\":\"spa_intex/info\",\"curr_temp_tpl\":\"{{ value_json.temp_2 }}\",\"act_t\":\"spa_intex/info\",\"act_tpl\":\"{{ 'heating' if value_json.heater else ('idle' if value_json.thermostat else 'off') }}\",\"min_temp\":20,\"max_temp\":39,\"temp_step\":1,\"temp_unit\":\"C\",\"uniq_id\":\"spa_climate\"," + deviceStr + "}";
  MQTTclient.publish(topicClimate.c_str(), payloadClimate.c_str(), true);

  // 3. Remove the legacy number + switch entities (clear their retained discovery).
  MQTTclient.publish("homeassistant/number/spa_intex/target/config", "", true);
  MQTTclient.publish("homeassistant/switch/spa_intex/thermostat/config", "", true);

  // 4. Filtration switch
  String topicFilt = "homeassistant/switch/spa_intex/filtration/config";
  String payloadFilt = "{\"name\":\"Spa Filtration\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ '1' if value_json.filtration else '0' }}\",\"cmd_t\":\"spa_intex/filtration\",\"pl_on\":\"1\",\"pl_off\":\"0\",\"uniq_id\":\"spa_filt\"," + deviceStr + "}";
  MQTTclient.publish(topicFilt.c_str(), payloadFilt.c_str(), true);

  // 5. Bubbles (Jet) switch
  String topicJet = "homeassistant/switch/spa_intex/jet/config";
  String payloadJet = "{\"name\":\"Spa Bubbles\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ '1' if value_json.jet else '0' }}\",\"cmd_t\":\"spa_intex/jet\",\"pl_on\":\"1\",\"pl_off\":\"0\",\"uniq_id\":\"spa_jet\"," + deviceStr + "}";
  MQTTclient.publish(topicJet.c_str(), payloadJet.c_str(), true);

  // 6. Water-flow interlock bypass (configuration category)
  String topicBpFlow = "homeassistant/switch/spa_intex/bypass_flow/config";
  String payloadBpFlow = "{\"name\":\"Spa Flow Bypass\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ '1' if value_json.bypass_flow else '0' }}\",\"cmd_t\":\"spa_intex/bypass_flow\",\"pl_on\":\"1\",\"pl_off\":\"0\",\"ent_cat\":\"config\",\"uniq_id\":\"spa_bypass_flow\"," + deviceStr + "}";
  MQTTclient.publish(topicBpFlow.c_str(), payloadBpFlow.c_str(), true);

  // 7. Thermal-fuse interlock bypass (configuration category)
  String topicBpFuse = "homeassistant/switch/spa_intex/bypass_fuse/config";
  String payloadBpFuse = "{\"name\":\"Spa Fuse Bypass\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ '1' if value_json.bypass_fuse else '0' }}\",\"cmd_t\":\"spa_intex/bypass_fuse\",\"pl_on\":\"1\",\"pl_off\":\"0\",\"ent_cat\":\"config\",\"uniq_id\":\"spa_bypass_fuse\"," + deviceStr + "}";
  MQTTclient.publish(topicBpFuse.c_str(), payloadBpFuse.c_str(), true);

  debugPrintln(String(C_GREEN) + "MQTT auto-discovery sent successfully!" + C_RESET);
}

// ==========================================
// MQTT LOGIC
// ==========================================
void connect_mqtt() {
  if (strlen(mqttServer) == 0) return;

  MQTTclient.setServer(mqttServer, mqttPort);
  MQTTclient.setCallback(mqttcallback);

  MQTTclient.setBufferSize(2048); // headroom for the larger climate discovery payload
  // Bound the blocking connect() (CONNACK wait) below the 8s watchdog so an
  // unreachable broker cannot stall loop() long enough to trigger a reboot.
  MQTTclient.setSocketTimeout(4);

  if (!MQTTclient.connected()) {
    debugPrint("Attempting MQTT connection: ");
    debugPrintln(mqttServer);

    if (MQTTclient.connect("IntexSpaClient", mqttUser, mqttPass)) {
      debugPrintln(String(C_GREEN) + "Connected to MQTT broker" + C_RESET);

      publishMQTTDiscovery();

      MQTTclient.subscribe(mqttTopicJet);
      MQTTclient.subscribe(mqttTopicFiltration);
      MQTTclient.subscribe(mqttTopicTarget);
      MQTTclient.subscribe(mqttTopicTempRegulation);
      MQTTclient.subscribe(mqttTopicReset);
      MQTTclient.subscribe(mqttTopicBypassFlow);
      MQTTclient.subscribe(mqttTopicBypassFuse);
    } else {
      debugPrint(String(C_RED) + "Error: MQTT state code = " + C_RESET);
      debugPrintln(MQTTclient.state());
    }
  }
}

void mqttcallback(char* topic, byte* payload, unsigned int length) {
  char msgBuffer[length + 1];
  memcpy(msgBuffer, payload, length);
  msgBuffer[length] = '\0';
  String message = String(msgBuffer);

  debugPrint(String(C_MAGENTA) + "MQTT -> [" + topic + "] : " + C_RESET);
  debugPrintln(message);

  if(strcmp(topic, mqttTopicTarget) == 0) {
    target_temp = message.toFloat();
    if(target_temp > max_temp) target_temp = max_temp;
    request_mqtt_update = true;
  }
  else if(strcmp(topic, mqttTopicJet) == 0) {
    jet_enabled = (message == "1");
    request_mqtt_update = true;
  }
  else if(strcmp(topic, mqttTopicFiltration) == 0) {
    filtration_enabled = (message == "1");
    request_mqtt_update = true;
  }
  else if(strcmp(topic, mqttTopicReset) == 0) {
    if(message == "1") {
      debugPrintln(String(C_YELLOW) + "Reboot requested via MQTT. Executing in 10 seconds..." + C_RESET);
      resetRequested = true;
      resetTimer = millis();
    }
  }
  else if(strcmp(topic, mqttTopicTempRegulation) == 0) {
    // Accepts "1"/"0" (switch) or "heat"/"off" (HA climate mode).
    if(message == "1" || message == "heat") {
      temp_regulation_enabled = true;
      filtration_enabled = true;
    } else {
      temp_regulation_enabled = false;
    }
    request_mqtt_update = true;
  }
  else if(strcmp(topic, mqttTopicBypassFlow) == 0) {
    bypass_flow = (message == "1");
    if(bypass_flow) debugPrintln(String(C_RED) + "WARNING: WATER FLOW interlock bypassed!" + C_RESET);
    request_mqtt_update = true;
  }
  else if(strcmp(topic, mqttTopicBypassFuse) == 0) {
    bypass_fuse = (message == "1");
    if(bypass_fuse) debugPrintln(String(C_RED) + "WARNING: THERMAL FUSE interlock bypassed!" + C_RESET);
    request_mqtt_update = true;
  }
}

// ==========================================
// HELPER METHODS
// ==========================================
void test_relay() {
  debugPrintln("Relay self-test...");
  digitalWrite(HEATER_1_PIN, HIGH); delay(200); digitalWrite(HEATER_1_PIN, LOW); delay(200);
  digitalWrite(HEATER_2_PIN, HIGH); delay(200); digitalWrite(HEATER_2_PIN, LOW); delay(200);
  digitalWrite(JET_PIN, HIGH);      delay(200); digitalWrite(JET_PIN, LOW);      delay(200);
  digitalWrite(PUMP_PIN, HIGH);     delay(200); digitalWrite(PUMP_PIN, LOW);     delay(200);
  debugPrintln(String(C_GREEN) + "Self-test finished." + C_RESET);
}

void activateOTA() {
  ArduinoOTA
    .onStart([]() {
      esp_task_wdt_delete(NULL);

      if (telnetClient) {
        telnetClient.println(String(C_YELLOW) + "OTA update detected. Telnet disconnect imminent..." + C_RESET);
        delay(100);
        telnetClient.stop();
      }

      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      Serial.println("Starting OTA update: " + type);
    })
    .onEnd([]() {
      Serial.println("\nOTA update finished");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("OTA error [%u]: ", error);
    });

  ArduinoOTA.setHostname("spa-intex");
  if(strlen(otaPass) > 0) {
    ArduinoOTA.setPassword(otaPass);
  } else {
    debugPrintln(String(C_RED) + "WARNING: OTA unprotected (no password configured)!" + C_RESET);
  }

  ArduinoOTA.begin();
}

void read_sensors() {
  fuse = (digitalRead(TEMP_FUSE_PIN) == FUSE_OK_LEVEL); // true = fuse intact (interlock OK)

  float raw_temp_1 = read_temperature(TEMP_1_PIN, SERIESRESISTOR_TEMP_1);
  float raw_temp_2 = read_temperature(TEMP_2_PIN, SERIESRESISTOR_TEMP_2);

  if(first_reading) {
    temp_1 = raw_temp_1;
    temp_2 = raw_temp_2;
    first_reading = false;
  } else {
    temp_1 = (temp_1 * (1.0 - smoothing_factor)) + (raw_temp_1 * smoothing_factor);
    temp_2 = (temp_2 * (1.0 - smoothing_factor)) + (raw_temp_2 * smoothing_factor);
  }

  current_temp = temp_2;

  bool new_flow_1 = (digitalRead(FLOW_1_PIN) == FLOW_ACTIVE_LEVEL);
  bool new_flow_2 = (digitalRead(FLOW_2_PIN) == FLOW_ACTIVE_LEVEL);

  if((new_flow_1 != flow_1) || (new_flow_2 != flow_2)) {
    request_mqtt_update = true;
  }

  flow_1 = new_flow_1;
  flow_2 = new_flow_2;
}

float read_temperature(int pin, int R1) {
  long sum = 0;

  for(int i = 0; i < num_samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(100);
  }

  float mes = (float)sum / num_samples;
  if (mes <= 0.0) return 0.0;

  float R2 = R1 * (4095.0 / mes - 1.0);
  float T = exp((R2 - 28589.0) / -7750.0);
  return T;
}

void update_relay_state() {
  activate_jet(jet_enabled);
  activate_filtration(filtration_enabled);
  activate_heating(must_heat());
}

void activate_jet(bool state) {
  digitalWrite(JET_PIN, state);
}

void activate_filtration(bool state) {
  digitalWrite(PUMP_PIN, state);
}

void activate_heating(bool state) {
  if(state && (flow_1 || bypass_flow) && (flow_2 || bypass_flow) && (fuse || bypass_fuse)) {
    if(!heating_enabled) {
      request_mqtt_update = true;
      heater1OnTime = heartbeat; // start the stagger timer
    }
    heating_enabled = true;
    digitalWrite(HEATER_1_PIN, HIGH);

    // Heater 2: only after the startup stagger AND never at the same time as the bubbles/jet
    bool heater2Allowed = (heartbeat - heater1OnTime >= heaterStageDelay) && !jet_enabled;
    digitalWrite(HEATER_2_PIN, heater2Allowed ? HIGH : LOW);
  } else {
    if(heating_enabled) request_mqtt_update = true;
    heating_enabled = false;
    digitalWrite(HEATER_1_PIN, LOW);
    digitalWrite(HEATER_2_PIN, LOW);
  }
}

bool must_heat() {
  if(!temp_regulation_enabled || (!flow_1 && !bypass_flow) || (!flow_2 && !bypass_flow) || (!fuse && !bypass_fuse) || current_temp >= max_temp) {
    if(heating_enabled) {
       lastHeatingStateChange = heartbeat;
    }
    return false;
  }

  bool desired_state = heating_enabled;

  if(current_temp <= (target_temp - hysterisys)) {
    desired_state = true;
  }
  else if(current_temp >= (target_temp + hysterisys)) {
    desired_state = false;
  }

  if(desired_state != heating_enabled) {
    if(isFirstCycle || (heartbeat - lastHeatingStateChange >= minHeatingCycle)) {
      lastHeatingStateChange = heartbeat;
      isFirstCycle = false;
      return desired_state;
    } else {
      return heating_enabled;
    }
  }

  return heating_enabled;
}

void update_mqtt_server() {
  JSONVar myData;
  myData["flow_1"] = (bool) flow_1;
  myData["flow_2"] = (bool) flow_2;

  myData["temp_1"] = round(temp_1 * 10.0) / 10.0;
  myData["temp_2"] = round(temp_2 * 10.0) / 10.0;
  myData["target"] = round(target_temp * 10.0) / 10.0;

  myData["heartbeat"] = (unsigned long) heartbeat;
  myData["thermostat"] = (bool) temp_regulation_enabled;
  myData["filtration"] = (bool) filtration_enabled;
  myData["heater"] = (bool) heating_enabled;
  myData["jet"] = (bool) jet_enabled;
  myData["fuse"] = (bool) fuse;
  myData["bypass_flow"] = (bool) bypass_flow;
  myData["bypass_fuse"] = (bool) bypass_fuse;

  String jsonString = JSON.stringify(myData);
  sendStringValueOverMQTT(jsonString, mqttTopicInfo);
}

void sendStringValueOverMQTT(String value, String topic) {
  if(mqtt_enable) {
    char valueChar[value.length() + 1];
    value.toCharArray(valueChar, value.length() + 1);

    char topicChar[topic.length() + 1];
    topic.toCharArray(topicChar, topic.length() + 1);

    MQTTclient.publish(topicChar, valueChar);
  }
}
