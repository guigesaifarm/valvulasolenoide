/**
 * Controle avançado de solenoides com proteções
 */

#ifndef SOLENOID_CONTROL_H
#define SOLENOID_CONTROL_H

#include "config.h"

class SolenoidController {
private:
    bool states[10];
    unsigned long startTimes[10];
    int scheduledDurations[10];
    
    // Staggering (evita ligar tudo ao mesmo tempo)
    const int STAGGER_DELAY_MS = 500;
    
public:
    void begin() {
        for (int i = 0; i < 10; i++) {
            pinMode(SOLENOID_PINS[i], OUTPUT);
            digitalWrite(SOLENOID_PINS[i], SOLENOID_OFF);
            states[i] = false;
            startTimes[i] = 0;
            scheduledDurations[i] = 0;
        }
        
        pinMode(PUMP_PIN, OUTPUT);
        digitalWrite(PUMP_PIN, LOW);
    }
    
    bool turnOn(int valve, int durationMinutes = 0, bool stagger = true) {
        if (valve < 1 || valve > 10) return false;
        
        int idx = valve - 1;
        
        // Se já está ligada, apenas atualiza timer
        if (states[idx]) {
            scheduledDurations[idx] = durationMinutes;
            return true;
        }
        
        // Staggering para evitar pico de corrente
        if (stagger && anyValveOn()) {
            delay(STAGGER_DELAY_MS);
        }
        
        digitalWrite(SOLENOID_PINS[idx], SOLENOID_ON);
        states[idx] = true;
        startTimes[idx] = millis();
        scheduledDurations[idx] = durationMinutes;
        
        updatePump();
        return true;
    }
    
    bool turnOff(int valve) {
        if (valve < 1 || valve > 10) return false;
        
        int idx = valve - 1;
        
        digitalWrite(SOLENOID_PINS[idx], SOLENOID_OFF);
        states[idx] = false;
        startTimes[idx] = 0;
        scheduledDurations[idx] = 0;
        
        updatePump();
        return true;
    }
    
    void turnOffAll() {
        for (int i = 1; i <= 10; i++) {
            turnOff(i);
            delay(100);
        }
    }
    
    bool isOn(int valve) {
        if (valve < 1 || valve > 10) return false;
        return states[valve - 1];
    }
    
    int getRunningMinutes(int valve) {
        if (valve < 1 || valve > 10 || !states[valve - 1]) return 0;
        return (millis() - startTimes[valve - 1]) / 60000;
    }
    
    void checkTimers() {
        for (int i = 0; i < 10; i++) {
            if (!states[i] || scheduledDurations[i] == 0) continue;
            
            int runningMin = (millis() - startTimes[i]) / 60000;
            if (runningMin >= scheduledDurations[i]) {
                Serial.printf("⏱️ Timer expirado: Válvula %d\n", i+1);
                turnOff(i+1);
            }
        }
    }
    
    void checkSafetyTimeout(unsigned long maxDurationMs) {
        for (int i = 0; i < 10; i++) {
            if (!states[i]) continue;
            
            if (millis() - startTimes[i] > maxDurationMs) {
                Serial.printf("⚠️ SAFETY: Válvula %d timeout\n", i+1);
                turnOff(i+1);
                // Aqui você pode enviar alerta MQTT
            }
        }
    }
    
private:
    bool anyValveOn() {
        for (int i = 0; i < 10; i++) {
            if (states[i]) return true;
        }
        return false;
    }
    
    void updatePump() {
        digitalWrite(PUMP_PIN, anyValveOn() ? HIGH : LOW);
    }
};

#endif
