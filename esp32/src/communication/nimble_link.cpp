#include <communication/nimble_link.h>

#if CONFIG_BT_NIMBLE_ENABLED

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <cstring>

static const char *TAG = "BLE";

// Nordic UART Service: the central writes to RX and is notified on TX.
static const char *SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char *TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

// Writes waiting for the link's task: a WiFi settings update is five 180-byte writes.
static constexpr UBaseType_t WRITE_QUEUE_LENGTH = 16;
// NimBLE waits 10 ms for a buffer on each try; its buffers drain one connection interval (up to 30 ms) at a time, so a
// reply of a few KB outlasts one try.
static constexpr int NOTIFY_ATTEMPTS = 20;

void NimbleLink::start(const std::string &name) {
    // The stack's host buffers live in PSRAM (CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL); without it there is none.
    if (!esp_psram_is_initialized()) {
        ESP_LOGW(TAG, "No PSRAM, so no Bluetooth");
        return;
    }
    const size_t internalBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    writes_ = xQueueCreateWithCaps(WRITE_QUEUE_LENGTH, sizeof(Write), MALLOC_CAP_SPIRAM);
    if (!writes_) {
        ESP_LOGE(TAG, "No memory for the write queue");
        return;
    }
    if (xTaskCreate(readTask, "BLE link", 8192, this, 3, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "No memory for the link's task, so no Bluetooth");
        vQueueDeleteWithCaps(writes_);
        writes_ = nullptr;
        return;
    }

    NimBLEDevice::init(name);
    NimBLEDevice::setMTU(MTU);

    auto *callbacks = new Callbacks(*this);
    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(callbacks);
    server_->advertiseOnDisconnect(true);

    NimBLEService *service = server_->createService(SERVICE_UUID);
    tx_ = service->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *rx = service->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(callbacks);

    // The 128-bit service UUID fills most of the 31-byte advertisement, so the name goes in the scan response.
    NimBLEAdvertisementData advertisement;
    advertisement.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advertisement.addServiceUUID(SERVICE_UUID);
    NimBLEAdvertisementData scanResponse;
    scanResponse.setName(name);
    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->setAdvertisementData(advertisement);
    advertising->setScanResponseData(scanResponse);
    advertising->enableScanResponse(true);
    // Starting to advertise starts the GATT server, and with it the service above.
    advertising->start();

    ESP_LOGI(TAG, "Advertising as %s; Bluetooth took %u bytes of internal RAM, %u left", name.c_str(),
             internalBefore - heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

// Runs on the NimBLE host task, while the decoder belongs to the link's task: writes still queued from the connection
// before are discarded, and the marker tells the link's task to drop its partial message.
void NimbleLink::markConnectionChange() {
    xQueueReset(writes_);
    const Write marker {};
    xQueueSendToFront(writes_, &marker, 0);
}

void NimbleLink::Callbacks::onConnect(NimBLEServer *server, NimBLEConnInfo &info) {
    link_.connHandle_ = info.getConnHandle();
    link_.markConnectionChange();
    link_.onConnected();
    // 15-30 ms intervals: a stick update a frame or two after it was sent. Supervision timeout 4 s.
    server->updateConnParams(info.getConnHandle(), 12, 24, 0, 400);
    ESP_LOGI(TAG, "Central connected");
}

void NimbleLink::Callbacks::onDisconnect(NimBLEServer *server, NimBLEConnInfo &info, int reason) {
    link_.connHandle_ = BLE_HS_CONN_HANDLE_NONE;
    link_.onDisconnected();
    link_.markConnectionChange();
    ESP_LOGI(TAG, "Central disconnected (reason %d), advertising", reason);
}

void NimbleLink::Callbacks::onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &info) {
    const NimBLEAttValue value = characteristic->getValue();
    if (value.length() == 0 || value.length() > MAX_WRITE) return;
    Write write;
    write.len = value.length();
    std::memcpy(write.data, value.data(), value.length());
    if (xQueueSend(link_.writes_, &write, 0) != pdTRUE) ESP_LOGW(TAG, "Write queue full, dropping a write");
}

void NimbleLink::readTask(void *self) {
    auto *link = static_cast<NimbleLink *>(self);
    Write write;
    for (;;) {
        if (xQueueReceive(link->writes_, &write, portMAX_DELAY) != pdTRUE) continue;
        if (write.len == 0) link->resetReceiver();
        else link->received(write.data, write.len);
    }
}

size_t NimbleLink::chunkSize() {
    const uint16_t mtu = server_->getPeerMTU(connHandle_);
    return mtu > MTU ? MAX_WRITE : mtu > BLE_ATT_MTU_DFLT ? mtu - 3 : BLE_ATT_MTU_DFLT - 3;
}

bool NimbleLink::notify(const uint8_t *chunk, size_t len) {
    for (int attempt = 0; attempt < NOTIFY_ATTEMPTS; attempt++) {
        const uint16_t handle = connHandle_;
        if (handle == BLE_HS_CONN_HANDLE_NONE) return false;
        if (tx_->notify(chunk, len, handle)) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGW(TAG, "Notification not sent after %d tries", NOTIFY_ATTEMPTS);
    return false;
}

void NimbleLink::dropConnection() {
    const uint16_t handle = connHandle_;
    if (handle != BLE_HS_CONN_HANDLE_NONE) server_->disconnect(handle);
}

#endif
