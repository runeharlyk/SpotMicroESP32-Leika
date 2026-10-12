#pragma once

#include <cstdarg>
#include <communication/comm_base.hpp>
#include <communication/serial_frame.h>
#include <communication/stall_tracker.h>

/**
 * The app's protocol over the USB serial ports, beside the firmware's log on the same ports. Frames and log lines go
 * out through one writer, so a line never splits a frame. Each port is one client, whose id is the port: the console
 * UART takes requests only (console_uart.h), and the USB-Serial-JTAG port, where the chip has one, carries the whole
 * protocol as the WebSocket does, streams included.
 */
class SerialLink : public CommAdapterBase {
  public:
    enum Port : int { UART = 0, USB = 1 };

    SerialLink();

    /** Takes over the console ports and the log; call first thing at boot. */
    void begin() override;

    /** Starts reading requests; call once every handler is registered. */
    void listen();

  private:
    // A USB host that has taken no stream frame for this long no longer reads the port: its subscriptions are dropped.
    static constexpr uint32_t STALL_LIMIT_MS = 10000;

    SemaphoreHandle_t _writeMutex;
    serial_frame::Decoder _uartDecoder;
    serial_frame::Decoder _usbDecoder;
    StallTracker _stalls {STALL_LIMIT_MS};

    bool send(const uint8_t *data, size_t len, int cid) override;
    bool sendStream(const uint8_t *data, size_t len, int cid) override;
    bool writeFrame(Port port, const uint8_t *data, size_t len, TickType_t wait);
    bool write(Port port, const uint8_t *data, size_t len, TickType_t wait);
    void writeLog(const char *line, size_t len);
    void dropUsbClient(const char *reason);

    static int logLine(const char *format, va_list args);
    static void readTask(void *self);
};
