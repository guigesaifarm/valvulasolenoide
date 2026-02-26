/**
 * Configurações de Hardware - AgroIrriga Pro
 * Adaptar conforme sua instalação específica
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

// ============ PINOS GPIO ESP32 ============

// Solenoides (10 válvulas) - Usar GPIOs que suportam saída digital
const int SOLENOID_PINS[10] = {
    4,   // Válvula 1  - GPIO 4
    5,   // Válvula 2  - GPIO 5
    13,  // Válvula 3  - GPIO 13
    14,  // Válvula 4  - GPIO 14
    15,  // Válvula 5  - GPIO 15
    16,  // Válvula 6  - GPIO 16
    17,  // Válvula 7  - GPIO 17
    18,  // Válvula 8  - GPIO 18
    19,  // Válvula 9  - GPIO 19
    21   // Válvula 10 - GPIO 21
};

// Bomba principal
#define PUMP_PIN 22

// Estação Meteorológica (SDI-12 ou Modbus RS485)
#define WEATHER_RX 26  // UART2 RX
#define WEATHER_TX 27  // UART2 TX
#define WEATHER_DE_RE 25  // Controle DE/RE para RS485

// Sensores adicionais
#define FLOW_SENSOR_PIN 32      // Sensor de fluxo de água (pulso)
#define PRESSURE_SENSOR_PIN 33  // Sensor de pressão (ADC)
#define RAIN_SENSOR_PIN 34      // Sensor de chuva (ADC ou digital)
#define SOIL_MOISTURE_PIN 35    // Sensor de umidade do solo (ADC)

// ============ CONFIGURAÇÕES DE HARDWARE ============

// Lógica dos solenoides (depende do driver)
#define SOLENOID_ON HIGH   // ou LOW se usar relé com lógica invertida
#define SOLENOID_OFF LOW   // ou HIGH

// Tensão e corrente
#define SOLENOID_VOLTAGE 12        // 12V ou 24V DC
#define SOLENOID_CURRENT_MA 250    // Corrente de cada solenoide em mA
#define PUMP_CURRENT_A 5.0         // Corrente da bomba em Amperes

// ============ WIFI CONFIG ============

#define WIFI_SSID "SUA_REDE_WIFI"
#define WIFI_PASSWORD "SUA_SENHA_WIFI"

// Ou usar WiFiManager para configuração via portal
#define USE_WIFIMANAGER true

// ============ MQTT CONFIG ============

#define MQTT_SERVER "mqtt.seuservidor.com"  // ou IP local
#define MQTT_PORT 1883
#define MQTT_USER "agroirriga"
#define MQTT_PASSWORD "senha_segura_aqui"

#define DEVICE_ID "agroirriga_fazenda_01"
#define MQTT_CLIENT_ID "agroirriga_" DEVICE_ID

// Tópicos
#define MQTT_TOPIC_COMMAND "agroirriga/" DEVICE_ID "/command"
#define MQTT_TOPIC_STATUS "agroirriga/" DEVICE_ID "/status"

// ============ TIMINGS ============

unsigned long WEATHER_READ_INTERVAL = 7200000;  // 2 horas padrão (em ms)
#define VALVE_SAFETY_TIMEOUT 7200000              // Máximo 2 horas contínuas
#define MQTT_RECONNECT_INTERVAL 5000              // 5 segundos entre tentativas

// ============ ESTRUTURAS DE DADOS ============

struct WeatherData {
    float temperature = 0;      // °C
    float humidity = 0;         // %
    float pressure = 0;         // hPa
    float windSpeed = 0;        // km/h
    float windDirection = 0;    // graus (0-360)
    float rainLastHour = 0;     // mm
    float rainToday = 0;        // mm
    float solarRadiation = 0;   // W/m²
    float uvIndex = 0;          // 0-11+
    char lastReadTime[25] = "never";
};

#endif
