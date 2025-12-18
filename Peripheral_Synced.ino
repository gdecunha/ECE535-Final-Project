#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BUTTON_PIN 6 


BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool waitingForSync = false;



uint64_t t1_send_us = 0;       
uint64_t t3_recv_us = 0;        

int64_t offset_est_us = 0;      
int64_t offset_prev_us = 0;

bool offset_initialized = false;


const float OFFSET_ALPHA = 0.005;   
uint64_t prev_sync_time_us = 0;
int64_t offset_epoch_us = 0;  
bool epoch_initialized = false;

int64_t g_current_offset_us = 0;

unsigned long long t_req_sent = 0; 

uint64_t get_synced_time_us() {
    uint64_t local_time = esp_timer_get_time();
   
    return (uint64_t)((int64_t)local_time + g_current_offset_us);
}


class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) {
      deviceConnected = true;
      Serial.println(">>> [BLE] Central Device CONNECTED!"); 
    };

    void onDisconnect(BLEServer *pServer) {
      deviceConnected = false;
      Serial.println(">>> [BLE] Central Device DISCONNECTED."); 
      
    }
};


class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {

    t3_recv_us = esp_timer_get_time();   // time packet is received

    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    Serial.print(">>> [RX] Received Data: ");
    Serial.println(value);

    if (value.startsWith("SYNC_ACK:") && waitingForSync) {

      // time from central
      uint64_t t2_master_us = 
          strtoull(value.substring(9).c_str(), NULL, 10);

     
      uint64_t rtt_us = t3_recv_us - t_req_sent;
      uint64_t one_way_us = rtt_us / 2;

      int64_t offset_meas_us =
          (int64_t)t2_master_us -
          (int64_t)(t_req_sent + one_way_us);

      // filtering
      if (!offset_initialized) {
        offset_est_us = offset_meas_us;
        offset_initialized = true;
      } else {
        offset_est_us +=
            (int64_t)(OFFSET_ALPHA * (offset_meas_us - offset_est_us));
      }

      int64_t residual_error_us = offset_meas_us - offset_est_us;

      // create epoch time
      if (!epoch_initialized) {
        offset_epoch_us = offset_est_us;  
        epoch_initialized = true;

        Serial.println(
          ">>> [EPOCH] Relative clock epoch established"
        );
      }


     double drift_ppm = 0.0;

    if (prev_sync_time_us != 0) {

      int64_t delta_offset = offset_est_us - offset_prev_us;
      uint64_t delta_t = t3_recv_us - prev_sync_time_us;  

      if (delta_t > 0) {
        drift_ppm =
            (double)delta_offset /
            (double)delta_t * 1e6;
      }

    int64_t relative_offset_us =
    offset_est_us - offset_epoch_us;

    Serial.printf(
      ">>> [SYNC] RTT=%llu us | RelOffset=%lld us | ResidualError=%lld us | Drift=%.3f ppm\n",
      rtt_us, relative_offset_us, residual_error_us, drift_ppm
    );
    Serial.printf("Delta Offset: %llu\n", delta_offset);
    Serial.printf("Synced Master Time: %llu\n", get_synced_time_us());




    } else {
      // first sync
      Serial.printf(
        ">>> [SYNC] RTT=%llu us | Drift=N/A (baseline)\n",
        rtt_us
      );
    }

    
    offset_prev_us = offset_est_us;
    prev_sync_time_us = t3_recv_us;


    g_current_offset_us = offset_est_us;

    waitingForSync = false;


     
    }
  }
};



void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT); 

  Serial.println("--- PERIPHERAL SETUP STARTING ---");

  BLEDevice::init("Drift_Peripheral");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | 
    BLECharacteristic::PROPERTY_WRITE | 
    BLECharacteristic::PROPERTY_NOTIFY 
  );
  pCharacteristic->setCallbacks(new MyCallbacks()); 
  pCharacteristic->addDescriptor(new BLE2902());
  
  pService->start();


  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
  Serial.println("--- PERIPHERAL ADVERTISING (Looking for connection...) ---");
}

void loop() {
    
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising(); 
        Serial.println(">>> [BLE] Restarting Advertising...");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
      
        oldDeviceConnected = deviceConnected;
        Serial.println(">>> [BLE] Connection Stable.");
    }

  
    static bool lastButton = LOW;
    bool currentButton = digitalRead(BUTTON_PIN);

    if (deviceConnected &&
        !waitingForSync &&
        currentButton == HIGH &&
        lastButton == LOW) {

        Serial.println(">>> [INPUT] Button Pressed. Requesting Time...");
        t_req_sent = esp_timer_get_time();
        waitingForSync = true;
        pCharacteristic->setValue("SYNC_REQ");
        pCharacteristic->notify();
    }

    lastButton = currentButton;

    
}

