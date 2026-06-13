#include <Arduino.h>
#include "esp_camera.h"
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid     = "TP-Link_ED1B";
const char* password = "14323508";
String serverName    = "http://sandworm-chaffing-cannot.ngrok-free.dev/api/upload";

#define SERVICE_UUID  "0000FFE0-0000-1000-8000-00805F9B34FB"
#define NOTIFY_UUID   "0000FFE1-0000-1000-8000-00805F9B34FB"
#define TARGET_NAME   "GARDIANT_CORE"

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
static bool ble_connected         = false;
static bool device_found          = false;
static BLEAddress* pServerAddress = nullptr;
static BLEClient* pClient         = nullptr;
static BLERemoteCharacteristic* pNotifyChar = nullptr;

void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  String data = "";
  for (size_t i = 0; i < length; i++) data += (char)pData[i];
  data.trim();
  Serial.printf("[BLE] Received: '%s'\n", data.c_str());
  if (data == "MOTION_OPEN") {
    Serial.println("[ALERT] Door OPENING -- Paging Camera Task!");
    xTaskNotifyGive(CameraTaskHandle);
  } else if (data == "MOTION_CLOSE") {
    Serial.println("[INFO] Door CLOSING -- Camera idle.");
  }
}

class ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* client) override    { ble_connected = true;  Serial.println("[BLE] Connected to GARDIANT_CORE!"); }
  void onDisconnect(BLEClient* client) override { ble_connected = false; Serial.println("[BLE] Disconnected -- will reconnect..."); }
};

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (advertisedDevice.getName() == TARGET_NAME) {
      advertisedDevice.getScan()->stop();
      if (pServerAddress) delete pServerAddress;
      pServerAddress = new BLEAddress(advertisedDevice.getAddress());
      device_found = true;
      Serial.println("[BLE] Target device found!");
    }
  }
};

void cameraTask(void *pvParameters) {
  Serial.printf("[SYSTEM] Camera Task active on Core %d\n", xPortGetCoreID());
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    Serial.println("[ACTION] Trigger received -- capturing frame...");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[ERROR] Camera capture failed!");
    } else {
      Serial.printf("[SUCCESS] Frame secured: %zu bytes\n", fb->len);
      if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverName);
        http.setTimeout(10000);
        http.addHeader("Content-Type", "image/jpeg");
        http.addHeader("ngrok-skip-browser-warning", "true");
        int code = http.POST(fb->buf, fb->len);
        if (code > 0) {
          Serial.printf("[HTTP] Code : %d\n", code);
          Serial.println("[HTTP] Reponse : " + http.getString());
        } else {
          Serial.printf("[HTTP] Echec : %s\n", http.errorToString(code).c_str());
        }
        http.end();
      } else {
        Serial.println("[WIFI] Deconnecte!");
      }
      esp_camera_fb_return(fb);
      Serial.println("[GARDIANT] Standby.\n");
    }
  }
}

void bleTask(void *pvParameters) {
  Serial.printf("[SYSTEM] BLE Task active on Core %d\n", xPortGetCoreID());
  for (;;) {
    if (!device_found) {
      BLEScan* pScan = BLEDevice::getScan();
      pScan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
      pScan->setActiveScan(true);
      pScan->setInterval(100);
      pScan->setWindow(99);
      pScan->start(10, false);
      if (!device_found) { pScan->clearResults(); vTaskDelay(3000 / portTICK_PERIOD_MS); continue; }
      pScan->clearResults();
    }
    if (pClient) { pClient->disconnect(); delete pClient; pClient = nullptr; }
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new ClientCallbacks());
    if (!pClient->connect(*pServerAddress)) { device_found = false; vTaskDelay(2000 / portTICK_PERIOD_MS); continue; }
    BLERemoteService* pService = pClient->getService(SERVICE_UUID);
    if (!pService) { pClient->disconnect(); device_found = false; vTaskDelay(2000 / portTICK_PERIOD_MS); continue; }
    pNotifyChar = pService->getCharacteristic(NOTIFY_UUID);
    if (!pNotifyChar) { pClient->disconnect(); device_found = false; vTaskDelay(2000 / portTICK_PERIOD_MS); continue; }
    if (pNotifyChar->canNotify()) {
      pNotifyChar->registerForNotify(notifyCallback);
      Serial.println("[BLE] Subscribed. GARDIANT_CORE fully linked!");
    } else { pClient->disconnect(); device_found = false; continue; }
    while (ble_connected) vTaskDelay(500 / portTICK_PERIOD_MS);
    device_found = false;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[GARDIANT] Booting Edge Core...");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_QVGA;
  config.jpeg_quality = 15;
  config.fb_count     = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("[FATAL] Camera init failed.");
    while (true) vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  Serial.println("[OK] Camera ready.");

  WiFi.begin(ssid, password);
  Serial.print("[WIFI] Connexion");
  while (WiFi.status() != WL_CONNECTED) { vTaskDelay(500 / portTICK_PERIOD_MS); Serial.print("."); }
  Serial.printf("\n[OK] WiFi! IP: %s\n", WiFi.localIP().toString().c_str());

  BLEDevice::init("GARDIANT_CAM");
  xTaskCreatePinnedToCore(cameraTask, "CameraTask", 8192, NULL, 1, &CameraTaskHandle, 1);
  xTaskCreatePinnedToCore(bleTask,    "BLETask",    8192, NULL, 1, NULL,              0);
  Serial.println("[GARDIANT] FreeRTOS tasks launched.");
}

void loop() { vTaskDelete(NULL); }