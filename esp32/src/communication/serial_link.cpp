#include <communication/serial_link.h>
#include <communication/console_uart.h>
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#include <esp_timer.h>
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
// A whole frame fits, so writing a reply never waits on the wire while holding the writer.
static constexpr size_t UART_TX_BUFFER = serial_frame::MAX_ENCODED + 512;
// Room for a telemetry batch while the one before it still goes out, beside the log.
static constexpr size_t USB_TX_BUFFER = 2 * serial_frame::MAX_ENCODED;
// A reply waits this long for room on a port; a stream frame or a log line does not wait for the USB port, which may
// have no reader.
static constexpr TickType_t FRAME_WAIT = pdMS_TO_TICKS(500);

static SerialLink *instance = nullptr;

SerialLink::SerialLink()
    : _writeMutex(xSemaphoreCreateMutex()),
      _uartDecoder([this](const uint8_t *m, size_t n) { handleIncoming(m, n, UART); }, [](const std::string &) {}),
      _usbDecoder([this](const uint8_t *m, size_t n) { handleIncoming(m, n, USB); }, [](const std::string &) {}) {
    decoder_.filter([](const socket_message_Message &message, int cid) {
        if (cid == USB || console_uart::accepts(message)) return true;
        static bool told = false;
        if (!told) ESP_LOGW(TAG, "The console UART takes requests only: driving and streams need USB or WiFi");
        told = true;
        return false;
    });
}

void SerialLink::begin() {
    instance = this;
    ESP_ERROR_CHECK(uart_driver_install(CONSOLE_UART, RX_BUFFER, UART_TX_BUFFER, 0, nullptr, 0));
    uart_vfs_dev_use_driver(CONSOLE_UART);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb.tx_buffer_size = USB_TX_BUFFER;
    usb.rx_buffer_size = RX_BUFFER;
    if (usb_serial_jtag_driver_install(&usb) == ESP_OK) usb_serial_jtag_vfs_use_driver();
#endif
    esp_log_set_vprintf(logLine);
}

void SerialLink::listen() { xTaskCreate(readTask, "Serial link", 8192, this, 2, nullptr); }

void SerialLink::readTask(void *self) {
    auto *link = static_cast<SerialLink *>(self);
    uint8_t buffer[256];
    [[maybe_unused]] bool usbHost = false;
    for (;;) {
        int n = uart_read_bytes(CONSOLE_UART, buffer, sizeof(buffer), pdMS_TO_TICKS(10));
        if (n > 0) link->_uartDecoder.feed(buffer, n);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
        if (usb_serial_jtag_is_driver_installed()) {
            // A host that is gone cannot unsubscribe; its streams would be encoded for nobody.
            const bool host = usb_serial_jtag_is_connected();
            if (usbHost && !host) link->dropUsbClient("the USB host is gone");
            usbHost = host;
            n = usb_serial_jtag_read_bytes(buffer, sizeof(buffer), pdMS_TO_TICKS(10));
            if (n > 0) link->_usbDecoder.feed(buffer, n);
        }
#endif
    }
}

bool SerialLink::send(const uint8_t *data, size_t len, int cid) {
    return writeFrame(static_cast<Port>(cid), data, len, FRAME_WAIT);
}

bool SerialLink::sendStream(const uint8_t *data, size_t len, int cid) {
    if (cid != USB) return send(data, len, cid);
    const bool written = writeFrame(USB, data, len, 0);
    if (_stalls.gone(USB, written, esp_timer_get_time() / 1000)) dropUsbClient("it has stopped reading");
    return written;
}

bool SerialLink::writeFrame(Port port, const uint8_t *data, size_t len, TickType_t wait) {
    const std::vector<uint8_t> frame = serial_frame::encode(data, len);
    if (frame.empty()) {
        ESP_LOGE(TAG, "Message of %u bytes is too large for a frame", len);
        return false;
    }
    xSemaphoreTake(_writeMutex, portMAX_DELAY);
    const bool written = write(port, frame.data(), frame.size(), wait);
    xSemaphoreGive(_writeMutex);
    return written;
}

// A frame goes out whole or not at all: the USB driver's byte ring takes all of it or nothing.
bool SerialLink::write(Port port, const uint8_t *data, size_t len, TickType_t wait) {
    if (port == UART) return uart_write_bytes(CONSOLE_UART, data, len) == static_cast<int>(len);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    return usb_serial_jtag_is_driver_installed() && usb_serial_jtag_is_connected() &&
           usb_serial_jtag_write_bytes(data, len, wait) == static_cast<int>(len);
#else
    return false;
#endif
}

void SerialLink::dropUsbClient(const char *reason) {
    clientGone(USB);
    _stalls.forget(USB);
    ESP_LOGI(TAG, "Dropped the USB client's subscriptions: %s", reason);
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
