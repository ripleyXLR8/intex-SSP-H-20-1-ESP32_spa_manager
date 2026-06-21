#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Arduino_JSON.h>
#include <esp_task_wdt.h> // Bibliotheque native pour le Watchdog
#include <WiFiManager.h>  // Bibliotheque pour le portail captif WiFi
#include <Preferences.h>  // Bibliotheque pour sauvegarder les parametres MQTT en memoire

// Configuration du Watchdog
#define WDT_TIMEOUT 8 // Temps de sursis en secondes avant reboot force

// Configuration des fonctionnalites
const bool wifi_enable = true;
const bool mqtt_enable = true;
const bool ota_enable = true;

// --- COULEURS ANSI POUR LE TERMINAL ---
#define C_RESET   "\033[0m"
#define C_RED     "\033[1;31m"
#define C_GREEN   "\033[1;32m"
#define C_YELLOW  "\033[1;33m"
#define C_CYAN    "\033[1;36m"
#define C_MAGENTA "\033[1;35m"

// Timers et intervalles
unsigned long previousHeartbeat = 0;
unsigned long previousSensorRead = 0;
unsigned long heartbeat;
const unsigned long sensor_read_interval = 500; 
const unsigned long mqtt_update_interval = 20000;

// Parametres du cycle de chauffe (Anti-Court-Cycle)
unsigned long lastHeatingStateChange = 0;
const unsigned long minHeatingCycle = 15 * 60 * 1000; // 15 minutes
bool isFirstCycle = true; 

// Variables pour le reset non-bloquant
bool resetRequested = false;
unsigned long resetTimer = 0;
const unsigned long resetDelay = 10000;

// ==========================================
// CONFIGURATION DYNAMIQUE (WIFIMANAGER)
// ==========================================
Preferences preferences;

char mqttServer[40] = "";
char mqttPortStr[6] = "1883";
int  mqttPort = 1883;
char mqttUser[32] = "";
char mqttPass[32] = "";

bool shouldSaveConfig = false;

void saveConfigCallback() {
  Serial.println("Changement de configuration detecte. Sauvegarde demandee.");
  shouldSaveConfig = true;
}

void loadConfig() {
  preferences.begin("spa_config", false);
  String s_server = preferences.getString("mqtt_server", "");
  String s_port   = preferences.getString("mqtt_port", "1883");
  String s_user   = preferences.getString("mqtt_user", "");
  String s_pass   = preferences.getString("mqtt_pass", "");
  
  strcpy(mqttServer, s_server.c_str());
  strcpy(mqttPortStr, s_port.c_str());
  strcpy(mqttUser, s_user.c_str());
  strcpy(mqttPass, s_pass.c_str());
  
  mqttPort = atoi(mqttPortStr);
  preferences.end();
}

void saveConfig() {
  preferences.begin("spa_config", false);
  preferences.putString("mqtt_server", mqttServer);
  preferences.putString("mqtt_port", mqttPortStr);
  preferences.putString("mqtt_user", mqttUser);
  preferences.putString("mqtt_pass", mqttPass);
  preferences.end();
  shouldSaveConfig = false;
}

// ==========================================
// VARIABLES D'ETAT
// ==========================================
bool request_mqtt_update = false;

// Serveur Telnet pour le Debug OTA
WiFiServer telnetServer(23);
WiFiClient telnetClient;

// Liste des Topics MQTT
const char* mqttTopicJet = "spa_intex/jet";
const char* mqttTopicFiltration = "spa_intex/filtration";
const char* mqttTopicTarget = "spa_intex/target";
const char* mqttTopicTempRegulation = "spa_intex/thermostat";
const char* mqttTopicHeaterForce = "spa_intex/heater";
const char* mqttTopicInfo = "spa_intex/info";
const char* mqttTopicReset = "spa_intex/reset";
const char* mqttTopicError = "spa_intex/error";

// Configuration des broches
#define HEATER_1_PIN 18 
#define HEATER_2_PIN 19 
#define JET_PIN 5 
#define PUMP_PIN 17 
#define FLOW_1_PIN 23 
#define FLOW_2_PIN 22 
#define TEMP_1_PIN 34 
#define TEMP_2_PIN 35 
#define TEMP_FUSE_PIN 32

// Niveau logique des entrees de securite (a verifier selon le cablage de la carte)
#define FLOW_ACTIVE_LEVEL HIGH   // niveau lu quand un debit d'eau est present
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

float hysterisys = 1.0; 
int max_temp = 39;

// ==========================================
// FONCTIONS DE DEBUG OTA
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
      // On efface l'ecran a la connexion (ANSI Clear Screen)
      telnetClient.print("\033[2J\033[H");
      telnetClient.println(String(C_GREEN) + "=== CONNEXION DEBUG OTA ETABLIE ===" + C_RESET);
    } else {
      telnetServer.available().stop();
    }
  }
}

String formatState(bool state, String tTrue, String tFalse) {
  return state ? (String(C_GREEN) + tTrue + C_RESET) : (String(C_RED) + tFalse + C_RESET);
}

// ==========================================
// INITIALISATION (SETUP)
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

  pinMode(0, INPUT_PULLUP); // GPIO 0 = Bouton BOOT de l'ESP32
  if (digitalRead(0) == LOW) { 
    Serial.println("Bouton BOOT maintenu : Effacement du WiFi !");
    wm.resetSettings(); 
  }

  WiFiManagerParameter custom_mqtt_server("server", "Serveur MQTT", mqttServer, 40);
  WiFiManagerParameter custom_mqtt_port("port", "Port MQTT", mqttPortStr, 6);
  WiFiManagerParameter custom_mqtt_user("user", "Utilisateur MQTT", mqttUser, 32);
  WiFiManagerParameter custom_mqtt_pass("pass", "Mot de passe MQTT", mqttPass, 32, "type='password'");

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);

  wm.setConfigPortalTimeout(180);

  Serial.println("Tentative de connexion WiFi...");
  
  if (!wm.autoConnect("Spa-Intex-Config")) {
    Serial.println("Echec connexion WiFi -> Mode Hors-Ligne active");
    offline_mode = true;
  } else {
    Serial.println("WiFi connecte !");
    offline_mode = false;

    strcpy(mqttServer, custom_mqtt_server.getValue());
    strcpy(mqttPortStr, custom_mqtt_port.getValue());
    strcpy(mqttUser, custom_mqtt_user.getValue());
    strcpy(mqttPass, custom_mqtt_pass.getValue());
    mqttPort = atoi(mqttPortStr);

    if (shouldSaveConfig) {
      saveConfig();
      Serial.println("Nouvelle configuration MQTT sauvegardee.");
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
  debugPrintln(String(C_GREEN) + "Watchdog active avec succes !" + C_RESET);
}

// ==========================================
// BOUCLE PRINCIPALE (LOOP)
// ==========================================
void loop() {
  heartbeat = millis();
  
  handleTelnet();

  if (resetRequested && (heartbeat - resetTimer >= resetDelay)) {
    debugPrintln(String(C_YELLOW) + "Reboot de l'ESP32 en cours..." + C_RESET);
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
    
    String strLock = String(C_GREEN) + "DEVERROUILLE" + C_RESET;
    if (elapsedSec < (minHeatingCycle / 1000)) {
      unsigned long remSec = (minHeatingCycle / 1000) - elapsedSec;
      strLock = String(C_YELLOW) + String(remSec / 60) + "m " + String(remSec % 60) + "s restants" + C_RESET;
    }
    if (isFirstCycle) strLock += " (Ignore pour 1er cycle)";

    String strDiag = String(C_GREEN) + "Fonctionnement normal" + C_RESET;
    if(temp_regulation_enabled && !heating_enabled) {
      if(!flow_1) strDiag = String(C_RED) + "BLOCAGE : Capteur d'eau a 0" + C_RESET;
      else if(current_temp >= max_temp) strDiag = String(C_RED) + "BLOCAGE : Securite 39C" + C_RESET;
      else if(current_temp >= (target_temp - hysterisys) && current_temp <= (target_temp + hysterisys)) strDiag = String(C_YELLOW) + "Zone morte (Hysteresis)" + C_RESET;
      else if(current_temp > target_temp) strDiag = String(C_CYAN) + "Eau a bonne temperature" + C_RESET;
      else if(!isFirstCycle && elapsedSec < (minHeatingCycle / 1000)) strDiag = String(C_YELLOW) + "Attente Anti-Court-Cycle" + C_RESET;
    }
    else if (temp_regulation_enabled && heating_enabled) {
      strDiag = String(C_GREEN) + "ETAT CHAUFFAGE : En chauffe normalement" + C_RESET;
    }

    debugPrintln();
    debugPrintln(String(C_CYAN) + "===================================================" + C_RESET);
    debugPrintln(String(C_CYAN) + "           SPA INTEX - TABLEAU DE BORD             " + C_RESET);
    debugPrintln(String(C_CYAN) + "===================================================" + C_RESET);
    
    debugPrintln("  Temperature  : " + String(C_YELLOW) + String(current_temp, 1) + " C" + C_RESET + " (Cible: " + String(C_YELLOW) + String(target_temp, 1) + " C" + C_RESET + ")");
    debugPrintln("  Thermostat   : [" + formatState(temp_regulation_enabled, "ACTIF", "INACTIF") + "]");
    debugPrintln("  Filtration   : [" + formatState(filtration_enabled, "ACTIVE", "INACTIVE") + "]");
    debugPrintln("  Bulles (Jet) : [" + formatState(jet_enabled, "ACTIVES", "INACTIVES") + "]");
    debugPrintln("  Chauffage    : [" + formatState(heating_enabled, "EN CHAUFFE", "ETEINT") + "]");
    debugPrintln("  Capteurs Eau : F1 [" + formatState(flow_1, "FORCÉ (1)", "ERREUR") + "]  |  F2 [" + formatState(flow_2, "FORCÉ (1)", "ERREUR") + "]");
    
    debugPrintln(String(C_CYAN) + "---------------------------------------------------" + C_RESET);
    debugPrintln("  Minuteur     : " + String(elMins) + "m " + String(elSecs) + "s ecoules");
    debugPrintln("  Verrou cycle : " + strLock);
    debugPrintln("  Diagnostic   : " + strDiag);
    debugPrintln(String(C_CYAN) + "===================================================" + C_RESET);
  }

  esp_task_wdt_reset(); 
}

// ==========================================
// AUTO-DECOUVERTE MQTT (HOME ASSISTANT)
// ==========================================
void publishMQTTDiscovery() {
  debugPrintln(String(C_CYAN) + "Envoi de l'Auto-Decouverte MQTT..." + C_RESET);

  String deviceStr = "\"dev\":{\"ids\":[\"spa_intex_esp32\"],\"name\":\"Spa Intex\",\"mdl\":\"DIY ESP32\",\"mf\":\"Custom\"}";

  // 1. Capteur de Temperature
  String topicTemp = "homeassistant/sensor/spa_intex/temperature/config";
  String payloadTemp = "{\"name\":\"Spa Temperature\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ value_json.temp_2 }}\",\"unit_of_meas\":\"°C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"uniq_id\":\"spa_temp\"," + deviceStr + "}";
  MQTTclient.publish(topicTemp.c_str(), payloadTemp.c_str(), true); 

  // 2. Reglage de la Temperature Cible
  String topicTarget = "homeassistant/number/spa_intex/target/config";
  String payloadTarget = "{\"name\":\"Spa Cible\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ value_json.target }}\",\"cmd_t\":\"spa_intex/target\",\"min\":20,\"max\":40,\"step\":1,\"unit_of_meas\":\"°C\",\"uniq_id\":\"spa_target\"," + deviceStr + "}";
  MQTTclient.publish(topicTarget.c_str(), payloadTarget.c_str(), true);

  // 3. Interrupteur Thermostat
  String topicTherm = "homeassistant/switch/spa_intex/thermostat/config";
  String payloadTherm = "{\"name\":\"Spa Thermostat\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ '1' if value_json.thermostat else '0' }}\",\"cmd_t\":\"spa_intex/thermostat\",\"pl_on\":\"1\",\"pl_off\":\"0\",\"uniq_id\":\"spa_therm\"," + deviceStr + "}";
  MQTTclient.publish(topicTherm.c_str(), payloadTherm.c_str(), true);

  // 4. Interrupteur Filtration
  String topicFilt = "homeassistant/switch/spa_intex/filtration/config";
  String payloadFilt = "{\"name\":\"Spa Filtration\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ '1' if value_json.filtration else '0' }}\",\"cmd_t\":\"spa_intex/filtration\",\"pl_on\":\"1\",\"pl_off\":\"0\",\"uniq_id\":\"spa_filt\"," + deviceStr + "}";
  MQTTclient.publish(topicFilt.c_str(), payloadFilt.c_str(), true);

  // 5. Interrupteur Bulles (Jet)
  String topicJet = "homeassistant/switch/spa_intex/jet/config";
  String payloadJet = "{\"name\":\"Spa Bulles\",\"stat_t\":\"spa_intex/info\",\"val_tpl\":\"{{ '1' if value_json.jet else '0' }}\",\"cmd_t\":\"spa_intex/jet\",\"pl_on\":\"1\",\"pl_off\":\"0\",\"uniq_id\":\"spa_jet\"," + deviceStr + "}";
  MQTTclient.publish(topicJet.c_str(), payloadJet.c_str(), true);
  
  debugPrintln(String(C_GREEN) + "Auto-Decouverte MQTT envoyee avec succes !" + C_RESET);
}

// ==========================================
// MQTT LOGIQUE
// ==========================================
void connect_mqtt() {
  if (strlen(mqttServer) == 0) return;

  MQTTclient.setServer(mqttServer, mqttPort);
  MQTTclient.setCallback(mqttcallback);
  
  MQTTclient.setBufferSize(1024);
 
  if (!MQTTclient.connected()) {
    debugPrint("Tentative de connexion MQTT : ");
    debugPrintln(mqttServer);
 
    if (MQTTclient.connect("IntexSpaClient", mqttUser, mqttPass)) {
      debugPrintln(String(C_GREEN) + "Connecte au broker MQTT" + C_RESET);
      
      publishMQTTDiscovery();
      
      MQTTclient.subscribe(mqttTopicJet);
      MQTTclient.subscribe(mqttTopicFiltration);
      MQTTclient.subscribe(mqttTopicTarget);
      MQTTclient.subscribe(mqttTopicTempRegulation);
      MQTTclient.subscribe(mqttTopicReset);
      MQTTclient.subscribe(mqttTopicHeaterForce);
    } else {
      debugPrint(String(C_RED) + "Erreur : Code etat MQTT = " + C_RESET);
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
  else if(strcmp(topic, mqttTopicHeaterForce) == 0) {
    if(message == "1" && flow_1) {
      digitalWrite(HEATER_1_PIN, HIGH);
      delay(500); 
      digitalWrite(HEATER_2_PIN, HIGH);
      heating_enabled = true;
      lastHeatingStateChange = millis();
      isFirstCycle = false;
    } else {
      digitalWrite(HEATER_1_PIN, LOW);
      delay(500);
      digitalWrite(HEATER_2_PIN, LOW);
      heating_enabled = false;
      lastHeatingStateChange = millis();
    }
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
      debugPrintln(String(C_YELLOW) + "Redemarrage demande via MQTT. Execution dans 10 secondes..." + C_RESET);
      resetRequested = true;
      resetTimer = millis();
    }
  }
  else if(strcmp(topic, mqttTopicTempRegulation) == 0) {
    if(message == "1") {
      temp_regulation_enabled = true;
      filtration_enabled = true; 
    } else {
      temp_regulation_enabled = false;
    }
    request_mqtt_update = true;
  }
}

// ==========================================
// METHODES AUXILIAIRES
// ==========================================
void test_relay() {
  debugPrintln("Auto-test des relais...");
  digitalWrite(HEATER_1_PIN, HIGH); delay(200); digitalWrite(HEATER_1_PIN, LOW); delay(200);
  digitalWrite(HEATER_2_PIN, HIGH); delay(200); digitalWrite(HEATER_2_PIN, LOW); delay(200);
  digitalWrite(JET_PIN, HIGH);      delay(200); digitalWrite(JET_PIN, LOW);      delay(200);
  digitalWrite(PUMP_PIN, HIGH);     delay(200); digitalWrite(PUMP_PIN, LOW);     delay(200);
  debugPrintln(String(C_GREEN) + "Fin de l'auto-test." + C_RESET);
}

void activateOTA() {
  ArduinoOTA
    .onStart([]() {
      esp_task_wdt_delete(NULL); 
      
      if (telnetClient) {
        telnetClient.println(String(C_YELLOW) + "Mise a jour OTA detectee. Deconnexion Telnet imminente..." + C_RESET);
        delay(100);
        telnetClient.stop(); 
      }
      
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      Serial.println("Debut mise a jour OTA : " + type);
    })
    .onEnd([]() {
      Serial.println("\nFin mise a jour OTA");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progression : %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Erreur OTA [%u]: ", error);
    });

  ArduinoOTA.begin();
}

void read_sensors() {
  fuse = digitalRead(TEMP_FUSE_PIN);
  
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
  if(state && flow_1) {
    if(!heating_enabled) request_mqtt_update = true;
    heating_enabled = true;
    digitalWrite(HEATER_1_PIN, HIGH);
    digitalWrite(HEATER_2_PIN, HIGH);
  } else {
    if(heating_enabled) request_mqtt_update = true;
    heating_enabled = false;
    digitalWrite(HEATER_1_PIN, LOW);
    digitalWrite(HEATER_2_PIN, LOW);
  }
}

bool must_heat() {
  if(!temp_regulation_enabled || !flow_1 || current_temp >= max_temp) {
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
