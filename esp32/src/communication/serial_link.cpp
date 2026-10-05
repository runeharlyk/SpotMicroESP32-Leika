#include <communication/serial_link.h>
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <soc/soc_caps.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#endif

static const char *TAG = "SerialLink";

static constexpr uart_port_t CONSOLE_UART = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
static constexpr size_t RX_BUFFER = 1024;
// A whole frame fits, so writing one never waits on the wire while holding the writer.
static constexpr size_t TX_BUFFER = serial_frame::MAX_ENCODED + 512;
// A reply waits this long for room on a port; a log line does not wait for the USB port, which may have no reader.
static constexpr TickType_t FRAME_WAIT = pdMS_TO_TICKS(500);

static SerialLink *instance = nullptr;

SerialLink::SerialLink()
    : _writeMutex(xSemaphoreCreateMutex()),
      _uartDecoder([this](const uint8_t *m, size_t n) { received(Port::UART, m, n); }, [](const std::string &) {}),
      _usbDecoder([this](const uint8_t *m, size_t n) { received(Port::USB, m, n); }, [](const std::string &) {}) {}

void SerialLink::begin() {
    instance = this;
    ESP_ERROR_CHECK(uart_driver_install(CONSOLE_UART, RX_BUFFER, TX_BUFFER, 0, nullptr, 0));
    uart_vfs_dev_use_driver(CONSOLE_UART);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb.tx_buffer_size = TX_BUFFER;
    usb.rx_buffer_size = RX_BUFFER;
    if (usb_serial_jtag_driver_install(&usb) == ESP_OK) usb_serial_jtag_vfs_use_driver();
#endif
    esp_log_set_vprintf(logLine);
}

void SerialLink::listen() { xTaskCreate(readTask, "Serial link", 8192, this, 2, nullptr); }

void SerialLink::readTask(void *self) {
    auto *link = static_cast<SerialLink *>(self);
    uint8_t buffer[256];
    for (;;) {
        int n = uart_read_bytes(CONSOLE_UART, buffer, sizeof(buffer), pdMS_TO_TICKS(10));
        if (n > 0) link->_uartDecoder.feed(buffer, n);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
        if (usb_serial_jtag_is_driver_installed()) {
            n = usb_serial_jtag_read_bytes(buffer, sizeof(buffer), pdMS_TO_TICKS(10));
            if (n > 0) link->_usbDecoder.feed(buffer, n);
        }
#endif
    }
}

void SerialLink::received(Port port, const uint8_t *message, size_t len) {
    _replyPort = port;
    handleIncoming(message, len, 0);
}

bool SerialLink::send(const uint8_t *data, size_t len, int cid) {
    const std::vector<uint8_t> frame = serial_frame::encode(data, len);
    if (frame.empty()) {
        ESP_LOGE(TAG, "Message of %u bytes is too large for a frame", len);
        return false;
    }
    xSemaphoreTake(_writeMutex, portMAX_DELAY);
    write(_replyPort, frame.data(), frame.size(), FRAME_WAIT);
    xSemaphoreGive(_writeMutex);
    return true;
}

void SerialLink::write(Port port, const uint8_t *data, size_t len, TickType_t wait) {
    if (port == Port::UART) {
        uart_write_bytes(CONSOLE_UART, data, len);
        return;
    }
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if (usb_serial_jtag_is_driver_installed() && usb_serial_jtag_is_connected())
        usb_serial_jtag_write_bytes(data, len, wait);
#endif
}

void SerialLink::writeLog(const char *line, size_t len) {
    xSemaphoreTake(_writeMutex, portMAX_DELAY);
    write(Port::UART, reinterpret_cast<const uint8_t *>(line), len, 0);
    write(Port::USB, reinterpret_cast<const uint8_t *>(line), len, 0);
    xSemaphoreGive(_writeMutex);
}

// The log's printer: each call is one line, written whole between frames.
int SerialLink::logLine(const char *format, va_list args) {
    char stackBuffer[256];
    va_list copy;
    va_copy(copy, args);
    const int len = vsnprintf(stackBuffer, sizeof(stackBuffer), format, copy);
    va_end(copy);
    if (len <= 0) return len;
    if (static_cast<size_t>(len) < sizeof(stackBuffer)) {
        instance->writeLog(stackBuffer, len);
        return len;
    }
    char *heapBuffer = static_cast<char *>(malloc(len + 1));
    if (!heapBuffer) {
        instance->writeLog(stackBuffer, sizeof(stackBuffer) - 1);
        return len;
    }
    vsnprintf(heapBuffer, len + 1, format, args);
    instance->writeLog(heapBuffer, len);
    free(heapBuffer);
    return len;
}
