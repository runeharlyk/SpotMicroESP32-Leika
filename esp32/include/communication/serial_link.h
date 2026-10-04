#pragma once

#include <atomic>
#include <cstdarg>
#include <communication/comm_base.hpp>
#include <communication/serial_frame.h>

/**
 * The app's protocol over the USB serial port, beside the firmware's log on the same port: settings and status, for
 * setting up a robot or one whose WiFi fails. Frames and log lines go out through one writer, so a line never splits a
 * frame. It listens on the console UART and, where the chip has one, the USB-Serial-JTAG port, and answers on the port
 * the last frame came from; there is one client, id 0.
 */
class SerialLink : public CommAdapterBase {
  public:
    SerialLink();

    /** Takes over the console ports and the log; call first thing at boot. */
    void begin() override;

    /** Starts reading requests; call once every handler is registered. */
    void listen();

  private:
    enum class Port { UART, USB };

    std::atomic<Port> _replyPort {Port::UART};
    SemaphoreHandle_t _writeMutex;
    serial_frame::Decoder _uartDecoder;
    serial_frame::Decoder _usbDecoder;

    bool send(const uint8_t *data, size_t len, int cid) override;
    void write(Port port, const uint8_t *data, size_t len, TickType_t wait);
    void writeLog(const char *line, size_t len);
    void received(Port port, const uint8_t *message, size_t len);

    static int logLine(const char *format, va_list args);
    static void readTask(void *self);
};
