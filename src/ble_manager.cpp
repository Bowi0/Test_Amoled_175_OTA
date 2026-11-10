#include "ble_manager.h"
#include <NimBLEDevice.h>
#include <Arduino.h>
#include "device_settings.h"

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static NimBLEServer *pServer = NULL;
static NimBLECharacteristic *pTxCharacteristic = NULL;
static volatile bool ble_initialized = false;
static volatile bool ble_active = false;
static volatile bool deviceConnected = false;

class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override { 
        deviceConnected = true; 
    }
    
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override { 
        deviceConnected = false;
        // Nur wenn BLE immer noch aktiv sein soll, Advertising neu starten
        if (ble_active && NimBLEDevice::getServer()) {
            NimBLEDevice::startAdvertising();
        }
    }
};

void ble_init(void) {
    if (ble_initialized) return;

    if(!NimBLEDevice::init("ESP32_Thermostat")) {
        Serial.println("Bluetooth: initialization failed");
        return;
    }
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    NimBLEService *pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(
                            CHARACTERISTIC_UUID_TX,
                            NIMBLE_PROPERTY::NOTIFY
                        );
    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName("Waveshare-Display"); // <-- DIESE ZEILE HINZUFÜGEN!
    pAdvertising->addServiceUUID(SERVICE_UUID);

    ble_initialized = true;
}

void ble_toggle(void) {
    ble_set_enabled(!ble_active);
}

bool ble_set_enabled(bool enabled) {
    if(enabled == ble_active) return true;
    if(enabled) {
        if(!ble_initialized) ble_init();
        if(!ble_initialized) return false;
        if(!NimBLEDevice::startAdvertising()) {
            Serial.println("Bluetooth: advertising could not be started");
            return false;
        }
        ble_active = true;
    } else {
        ble_active = false;
        if(!NimBLEDevice::stopAdvertising()) {
            Serial.println("Bluetooth: advertising could not be stopped");
            ble_active = true;
            return false;
        }
        for(uint16_t peer : pServer->getPeerDevices()) {
            if(!pServer->disconnect(peer)) {
                Serial.println("Bluetooth: failed to disconnect peer");
                ble_active = true;
                NimBLEDevice::startAdvertising();
                return false;
            }
        }
        deviceConnected = false;
    }
    if(device_bluetooth_enabled() != enabled &&
       !device_set_bluetooth_enabled(enabled)) {
        Serial.println("Bluetooth: state was not saved");
        return false;
    }
    return true;
}

bool ble_is_active(void) {
    return ble_active;
}

void ble_send_pressure(float pressure) {
    // Harte Schutzabfrage vor Zugriffen
    if (!ble_initialized || !ble_active || !deviceConnected || pTxCharacteristic == NULL) {
        return;
    }

    char buffer[32];
    int len = snprintf(buffer, sizeof(buffer), "%.2f\n", pressure);
    if (len > 0) {
        pTxCharacteristic->setValue((uint8_t*)buffer, len);
        pTxCharacteristic->notify();
    }
}