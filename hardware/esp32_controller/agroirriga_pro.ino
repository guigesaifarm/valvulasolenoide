/**
 * AgroIrriga Pro - Sistema de Automação de Irrigação
 * Hardware: ESP32-WROOM-32D
 * Funções: Controle de 10 solenoides, leitura estação meteorológica, MQTT
 * Autor: GuiGesaifarm
 * Versão: 2.0
 */

#include "config.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "solenoid_control.h"
#include "weather_station.h"

// ============ CONFIGURAÇÕES GLOBAIS ============

// Estados das válvulas (10 solenoides)
bool valveStates[10] = {false, false, false, false, false, 
                        false, false, false, false, false};

// Tempos de irrigação por linha (minutos)
int irrigationTimes[10] = {30, 30, 30, 30, 30, 30, 30, 30, 30, 30};

// Dados meteorológicos
WeatherData currentWeather;

// Timers para non-blocking code
unsigned long lastWeatherRead = 0;
unsigned long lastHeartbeat = 0;
unsigned long valveStartTimes[10] = {0};

// ============ SETUP ============

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n🚜 AGROIRRIGA PRO - Iniciando sistema...");
    Serial.println("==========================================");
    
    // Inicializa pinos dos solenoides
    initSolenoids();
    
    // Conecta WiFi
    if (!connectWiFi()) {
        Serial.println("❌ Falha WiFi! Reiniciando...");
        delay(5000);
        ESP.restart();
    }
    
    // Configura MQTT
    setupMQTT();
    
    // Inicializa estação meteorológica
    initWeatherStation();
    
    // Sincroniza hora via NTP (para logs e agendamentos)
    configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    
    Serial.println("✅ Sistema iniciado com sucesso!");
    Serial.println("==========================================\n");
}

// ============ LOOP PRINCIPAL ============

void loop() {
    // Mantém conexão MQTT
    if (!mqttConnected()) {
        reconnectMQTT();
    }
    mqttLoop();

    // Verifica comandos da serial (debug)
    handleSerialCommands();

    // Leitura da estação meteorológica a cada 2 horas (7200000 ms)
    if (millis() - lastWeatherRead > WEATHER_READ_INTERVAL) {
        readWeatherStation();
        lastWeatherRead = millis();
    }

    // Verifica segurança (timeout de válvulas)
    checkValveSafety();

    // Envia heartbeat a cada 30 segundos
    if (millis() - lastHeartbeat > 30000) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }

    delay(100); // Pequeno delay para não sobrecarregar CPU
}

// ============ FUNÇÕES DE CONTROLE ============

void initSolenoids() {
    Serial.println("🔧 Inicializando solenoides...");
    
    for (int i = 0; i < 10; i++) {
        pinMode(SOLENOID_PINS[i], OUTPUT);
        digitalWrite(SOLENOID_PINS[i], SOLENOID_OFF); // Garante que começam desligados
        
        // Configura PWM para controle de pressão (se necessário)
        ledcSetup(i, 1000, 8); // Canal, frequência 1kHz, resolução 8 bits
        ledcAttachPin(SOLENOID_PINS[i], i);
        
        Serial.printf("  ✓ Válvula %d -> GPIO %d\n", i+1, SOLENOID_PINS[i]);
    }
    
    // Pino da bomba principal
    pinMode(PUMP_PIN, OUTPUT);
    digitalWrite(PUMP_PIN, LOW);
    
    Serial.println("✅ Solenoides inicializados\n");
}

void setValve(int valveNumber, bool state, int durationMinutes = 0) {
    if (valveNumber < 1 || valveNumber > 10) {
        Serial.printf("❌ Válvula %d inválida!\n", valveNumber);
        return;
    }
    
    int index = valveNumber - 1;
    
    if (state) {
        // Liga válvula
        digitalWrite(SOLENOID_PINS[index], SOLENOID_ON);
        valveStates[index] = true;
        valveStartTimes[index] = millis();
        
        // Liga bomba se alguma válvula está aberta
        updatePumpState();
        
        Serial.printf("💧 VÁLVULA %d LIGADA", valveNumber);
        if (durationMinutes > 0) {
            Serial.printf(" (Timer: %d min)", durationMinutes);
            irrigationTimes[index] = durationMinutes;
        }
        Serial.println();
        
        // Envia confirmação MQTT
        publishValveState(valveNumber, true);
        
    } else {
        // Desliga válvula
        digitalWrite(SOLENOID_PINS[index], SOLENOID_OFF);
        valveStates[index] = false;
        valveStartTimes[index] = 0;
        
        // Verifica se precisa desligar bomba
        updatePumpState();
        
        Serial.printf("🔒 Válvula %d desligada\n", valveNumber);
        
        // Envia confirmação MQTT
        publishValveState(valveNumber, false);
    }
}

void updatePumpState() {
    bool anyValveOpen = false;
    for (int i = 0; i < 10; i++) {
        if (valveStates[i]) {
            anyValveOpen = true;
            break;
        }
    }
    
    digitalWrite(PUMP_PIN, anyValveOpen ? HIGH : LOW);
    
    if (anyValveOpen) {
        Serial.println("🔌 Bomba principal LIGADA");
    } else {
        Serial.println("⭕ Bomba principal desligada");
    }
}

void checkValveSafety() {
    // Verifica timeout de segurança (máximo 2 horas por válvula)
    const unsigned long MAX_VALVE_TIME = 7200000; // 2 horas em ms
    
    for (int i = 0; i < 10; i++) {
        if (valveStates[i] && (millis() - valveStartTimes[i] > MAX_VALVE_TIME)) {
            Serial.printf("⚠️ SAFETY: Válvula %d excedeu tempo máximo! Desligando...\n", i+1);
            setValve(i+1, false);
            
            // Envia alerta
            publishAlert("SAFETY_TIMEOUT", i+1);
        }
        
        // Verifica timer programado
        if (valveStates[i] && irrigationTimes[i] > 0) {
            unsigned long elapsedMinutes = (millis() - valveStartTimes[i]) / 60000;
            if (elapsedMinutes >= irrigationTimes[i]) {
                Serial.printf("⏱️ Timer: Válvula %d completou %d minutos. Desligando.\n", 
                             i+1, irrigationTimes[i]);
                setValve(i+1, false);
            }
        }
    }
}

// ============ COMUNICAÇÃO MQTT ============

void handleMQTTMessage(char* topic, byte* payload, unsigned int length) {
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.printf("📨 MQTT [%s]: %s\n", topic, message.c_str());
    
    // Parse JSON
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.printf("❌ Erro JSON: %s\n", error.c_str());
        return;
    }
    
    String action = doc["action"] | "";
    
    if (action == "valve_on") {
        int valve = doc["valve"] | 0;
        int duration = doc["duration"] | 0;
        setValve(valve, true, duration);
    }
    else if (action == "valve_off") {
        int valve = doc["valve"] | 0;
        setValve(valve, false);
    }
    else if (action == "valve_all_off") {
        allValvesOff();
    }
    else if (action == "schedule_irrigation") {
        // Agendamento via JSON
        int valve = doc["valve"] | 0;
        int duration = doc["duration"] | 30;
        int startHour = doc["start_hour"] | 6;
        int startMinute = doc["start_minute"] | 0;
        
        scheduleIrrigation(valve, duration, startHour, startMinute);
    }
    else if (action == "get_status") {
        publishFullStatus();
    }
    else if (action == "set_weather_interval") {
        // Atualiza intervalo de leitura meteorológica
        int minutes = doc["minutes"] | 120;
        WEATHER_READ_INTERVAL = minutes * 60000;
        Serial.printf("🌤️ Intervalo meteorológico: %d minutos\n", minutes);
    }
}

void publishValveState(int valveNumber, bool state) {
    StaticJsonDocument<256> doc;
    doc["valve"] = valveNumber;
    doc["state"] = state ? "ON" : "OFF";
    doc["timestamp"] = getISO8601Time();
    doc["uptime_ms"] = millis();
    
    char buffer[256];
    serializeJson(doc, buffer);
    mqttPublish("agroirriga/valve/status", buffer);
}

void publishFullStatus() {
    StaticJsonDocument<1024> doc;
    
    JsonArray valves = doc.createNestedArray("valves");
    for (int i = 0; i < 10; i++) {
        JsonObject v = valves.createNestedObject();
        v["number"] = i + 1;
        v["state"] = valveStates[i] ? "ON" : "OFF";
        v["pin"] = SOLENOID_PINS[i];
        if (valveStates[i]) {
            v["running_minutes"] = (millis() - valveStartTimes[i]) / 60000;
            v["scheduled_minutes"] = irrigationTimes[i];
        }
    }
    
    doc["pump_state"] = digitalRead(PUMP_PIN) == HIGH ? "ON" : "OFF";
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["timestamp"] = getISO8601Time();
    
    // Adiciona dados meteorológicos atuais
    JsonObject weather = doc.createNestedObject("weather");
    weather["temperature"] = currentWeather.temperature;
    weather["humidity"] = currentWeather.humidity;
    weather["pressure"] = currentWeather.pressure;
    weather["wind_speed"] = currentWeather.windSpeed;
    weather["rain_1h"] = currentWeather.rainLastHour;
    weather["solar_radiation"] = currentWeather.solarRadiation;
    weather["last_read"] = currentWeather.lastReadTime;
    
    char buffer[1024];
    serializeJson(doc, buffer);
    mqttPublish("agroirriga/system/status", buffer);
    
    Serial.println("📤 Status completo publicado");
}

void sendHeartbeat() {
    StaticJsonDocument<256> doc;
    doc["type"] = "heartbeat";
    doc["uptime_minutes"] = millis() / 60000;
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["timestamp"] = getISO8601Time();
    
    char buffer[256];
    serializeJson(doc, buffer);
    mqttPublish("agroirriga/system/heartbeat", buffer);
}

void publishAlert(const char* type, int valveNumber) {
    StaticJsonDocument<256> doc;
    doc["type"] = "alert";
    doc["alert_type"] = type;
    doc["valve"] = valveNumber;
    doc["message"] = String("Alerta de segurança na válvula ") + valveNumber;
    doc["timestamp"] = getISO8601Time();
    
    char buffer[256];
    serializeJson(doc, buffer);
    mqttPublish("agroirriga/alerts", buffer);
}

// ============ FUNÇÕES AUXILIARES ============

void allValvesOff() {
    Serial.println("🛑 Desligando TODAS as válvulas...");
    for (int i = 1; i <= 10; i++) {
        setValve(i, false);
        delay(100); // Pequeno delay entre válvulas para não sobrecarregar PSU
    }
}

void scheduleIrrigation(int valve, int duration, int hour, int minute) {
    // Implementação simplificada - em produção usar RTC ou NTP
    Serial.printf("📅 Agendado: Válvula %d às %02d:%02d por %d minutos\n", 
                 valve, hour, minute, duration);
    // Aqui você integraria com um agendador RTC ou verificaria no loop
}

void handleSerialCommands() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    
    if (cmd.startsWith("v")) {
        // Comando: v1on, v1off, v5on30 (válvula 5 on por 30 min)
        int valve = cmd.substring(1, 2).toInt();
        bool state = cmd.indexOf("on") > 0;
        int duration = 0;
        
        // Extrai duração se especificada (ex: v3on45 = 45 min)
        if (state && cmd.length() > 4) {
            duration = cmd.substring(4).toInt();
        }
        
        if (valve >= 1 && valve <= 10) {
            setValve(valve, state, duration);
        }
    }
    else if (cmd == "status") {
        publishFullStatus();
    }
    else if (cmd == "weather") {
        readWeatherStation();
        printWeatherData();
    }
    else if (cmd == "alloff") {
        allValvesOff();
    }
    else if (cmd == "help") {
        Serial.println("\n=== COMANDOS DISPONÍVEIS ===");
        Serial.println("v1on     - Liga válvula 1");
        Serial.println("v1off    - Desliga válvula 1");
        Serial.println("v3on45   - Liga válvula 3 por 45 minutos");
        Serial.println("status   - Mostra status completo");
        Serial.println("weather  - Lê estação meteorológica");
        Serial.println("alloff   - Desliga todas as válvulas");
        Serial.println("help     - Mostra esta ajuda");
        Serial.println("===========================\n");
    }
}

String getISO8601Time() {
    time_t now;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "1970-01-01T00:00:00Z";
    }
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(buf);
}
