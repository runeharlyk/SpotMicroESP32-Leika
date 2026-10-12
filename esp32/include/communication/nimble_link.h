#pragma once

#include <sdkconfig.h>

#if CONFIG_BT_NIMBLE_ENABLED

#include <NimBLEDevice.h>
#include <freertos/queue.h>
#include <string>
#include <communication/ble_link.h>

/**
 * The app's protocol over a Nordic UART Service on the chip's own radio, through esp-nimble-cpp, as Hexapod runs it.
 * Only builds whose sdkconfig enables NimBLE have it (sdkconfig.defaults.ble): the stack wants PSRAM for its buffers.
 * Writes reach a task of the link's own, which decodes them and runs their handlers, as the serial link's does.
 */
class NimbleLink : public BleLink {
  public:
    /** Advertises as `name` and starts taking writes; call once every handler is registered. Needs PSRAM. */
    void start(const std::string &name);

  private:
    // The ATT MTU asked for: 247 fits one LE packet once the controller has extended the data length.
    static constexpr uint16_t MTU = 247;
    static constexpr size_t MAX_WRITE = MTU - 3;

    // A write as the central sent it; a length of 0 marks a connection that began or ended.
    struct Write {
        uint16_t len;
        uint8_t data[MAX_WRITE];
    };

    class Callbacks : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
      public:
        explicit Callbacks(NimbleLink &link) : link_(link) {}
        void onConnect(NimBLEServer *server, NimBLEConnInfo &info) override;
        void onDisconnect(NimBLEServer *server, NimBLEConnInfo &info, int reason) override;
        void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &info) override;

      private:
        NimbleLink &link_;
    };

    QueueHandle_t writes_ = nullptr;
    NimBLEServer *server_ = nullptr;
    NimBLECharacteristic *tx_ = nullptr;
    std::atomic<uint16_t> connHandle_ {BLE_HS_CONN_HANDLE_NONE};

    void markConnectionChange();

    size_t chunkSize() override;
    bool notify(const uint8_t *chunk, size_t len) override;
    void dropConnection() override;

    static void readTask(void *self);
};

#endif
