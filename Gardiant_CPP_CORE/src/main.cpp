#include <Arduino.h>
#include "esp_camera.h"
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// UUIDs  BT04  documenation
#define SERVICE_UUID  "0000FFE0-0000-1000-8000-00805F9B34FB"
#define NOTIFY_UUID   "0000FFE1-0000-1000-8000-00805F9B34FB"

#define TARGET_NAME   "GARDIANT_CORE"

// CAMERA PINS (AI-Thinker)
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

TaskHandle_t CameraTaskHandle;
static bool ble_connected        = false;
static bool device_found         = false;
static BLEAddress* pServerAddress = nullptr;
static BLEClient*  pClient        = nullptr;
static BLERemoteCharacteristic* pNotifyChar = nullptr;

void notifyCallback(BLERemoteCharacteristic* pChar,
                    uint8_t* pData,
                    size_t length,
                    bool isNotify) {

  String data = "";
  for (size_t i = 0; i < length; i++) {
    data += (char)pData[i];
  }
  data.trim();

  Serial.printf("[BLE] Received: '%s'\n", data.c_str());

  if (data == "MOTION_OPEN") {
    Serial.println("[ALERT] Door OPENING -- Paging Camera Task!");
    xTaskNotifyGive(CameraTaskHandle);
  }
  else if (data == "MOTION_CLOSE") {
    Serial.println("[INFO] Door CLOSING -- Camera idle.");
  }
}

class ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* client) override {
    Serial.println("[BLE] Connected to GARDIANT_CORE!");
    ble_connected = true;
  }
  void onDisconnect(BLEClient* client) override {
    Serial.println("[BLE] Disconnected -- will reconnect...");
    ble_connected = false;
  }
};

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    Serial.printf("[BLE] Found: %s\n",
                  advertisedDevice.toString().c_str());

    if (advertisedDevice.getName() == TARGET_NAME) {
      Serial.println("[BLE] Target device found! Stopping scan...");
      advertisedDevice.getScan()->stop();
      if (pServerAddress) delete pServerAddress;
      pServerAddress = new BLEAddress(advertisedDevice.getAddress());
      device_found = true;
    }
  }
};

void cameraTask(void *pvParameters) {
  Serial.printf("[SYSTEM] Camera Task active on Core %d\n",
                xPortGetCoreID());

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    Serial.println("\n[ACTION] Trigger received -- capturing frame...");

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[ERROR] Camera capture failed!");
    } else {
      Serial.printf("[SUCCESS] Frame secured: %zu bytes\n", fb->len);

      esp_camera_fb_return(fb);
      Serial.println("[GARDIANT] Frame released. Returning to standby.\n");
    }
  }
}

void bleTask(void *pvParameters) {
  Serial.printf("[SYSTEM] BLE Task active on Core %d\n",
                xPortGetCoreID());

  for (;;) {
    if (!device_found) {
      Serial.println("[BLE] Scanning for GARDIANT_CORE...");
      device_found = false;

      BLEScan* pScan = BLEDevice::getScan();
      pScan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
      pScan->setActiveScan(true);
      pScan->setInterval(100);
      pScan->setWindow(99);
      pScan->start(10, false); // Scan for 10 seconds

      if (!device_found) {
        Serial.println("[BLE] Device not found. Retrying in 3s...");
        pScan->clearResults();
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        continue;
      }
      pScan->clearResults();
    }

    Serial.println("[BLE] Connecting...");

    if (pClient) {
      pClient->disconnect();
      delete pClient;
      pClient = nullptr;
    }

    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new ClientCallbacks());

    if (!pClient->connect(*pServerAddress)) {
      Serial.println("[BLE] Connection failed. Re-scanning...");
      device_found = false;
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      continue;
    }

    Serial.println("[BLE] Discovering services...");

    BLERemoteService* pService =
        pClient->getService(SERVICE_UUID);

    if (!pService) {
      Serial.println("[BLE] Service FFE0 not found! Re-scanning...");
      pClient->disconnect();
      device_found = false;
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      continue;
    }

    pNotifyChar = pService->getCharacteristic(NOTIFY_UUID);

    if (!pNotifyChar) {
      Serial.println("[BLE] Characteristic FFE1 not found! Re-scanning...");
      pClient->disconnect();
      device_found = false;
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      continue;
    }

    if (pNotifyChar->canNotify()) {
      pNotifyChar->registerForNotify(notifyCallback);
      Serial.println("[BLE] Subscribed to notifications.");
      Serial.println("[BLE] GARDIANT_CORE fully linked! Listening...");
    } else {
      Serial.println("[BLE] Characteristic cannot notify! Re-scanning...");
      pClient->disconnect();
      device_found = false;
      continue;
    }


    while (ble_connected) {
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    Serial.println("[BLE] Re-entering scan phase...");
    device_found = false;
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("\n[GARDIANT] Booting Edge Core...");

  camera_config_t config;
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sccb_sda  = SIOD_GPIO_NUM;
  config.pin_sccb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = RESET_GPIO_NUM;
  config.xclk_freq_hz  = 20000000;
  config.pixel_format  = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count     = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("[FATAL] Camera init failed. Halting.");
    while (true) vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  Serial.println("[OK] Camera ready.");

  BLEDevice::init("GARDIANT_CAM");
  Serial.println("[OK] BLE initialized.");

  xTaskCreatePinnedToCore(
    cameraTask, "CameraTask",
    8192, NULL, 1,
    &CameraTaskHandle, 1
  );
  xTaskCreatePinnedToCore(
    bleTask, "BLETask",
    8192, NULL, 1,
    NULL, 0
  );

  Serial.println("[GARDIANT] FreeRTOS tasks launched. Entering event loop.");
}

void loop() {
  vTaskDelete(NULL);
}