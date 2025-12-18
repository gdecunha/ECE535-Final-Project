#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID           "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteChar = nullptr;
bool connected = false;

// CALLBACK: Handles incoming data from the Peripheral
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    String msg = String((char*)pData, length);
    unsigned long long myTime = esp_timer_get_time(); 

    if (msg == "SYNC_REQ") {
        if (pRemoteChar != nullptr) {
            String reply = "SYNC_ACK:" + String((unsigned long)myTime); 
            pRemoteChar->writeValue((uint8_t*)reply.c_str(), reply.length(), false);
        }
    } 
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {}
    void onDisconnect(BLEClient* pclient) { 
        connected = false; 
    }
};

void setup() {
    Serial.begin(115200);
    BLEDevice::init("Central_Master");
}

void loop() {
    if (!connected) {
        BLEScan* pBLEScan = BLEDevice::getScan();
        pBLEScan->setActiveScan(true);
        BLEScanResults* foundDevices = pBLEScan->start(3, false);
        
        for (int i = 0; i < foundDevices->getCount(); i++) {
            BLEAdvertisedDevice device = foundDevices->getDevice(i);
            
            if (device.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
                pClient = BLEDevice::createClient();
                pClient->setClientCallbacks(new MyClientCallback());
                
                if (pClient->connect(&device)) {
                    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
                    if (pRemoteService != nullptr) {
                        pRemoteChar = pRemoteService->getCharacteristic(CHAR_UUID);
                        if (pRemoteChar != nullptr) {
                            pRemoteChar->registerForNotify(notifyCallback);
                            connected = true;
                            Serial.println("Connected and registered for notifications.");
                            break; 
                        }
                    }
                    pClient->disconnect();
                }
            }
        }
        pBLEScan->clearResults();
    }
    
    delay(100); 
}
