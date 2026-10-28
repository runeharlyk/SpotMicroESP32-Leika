#pragma once

#include <platform_shared/message.pb.h>

/**
 * What the serial link takes from a client on the console UART. That port runs at 115200 baud and carries the log as
 * well, too slow for streams, so it takes requests and pings and a subscription to the WiFi status only; driving and
 * streams need the USB-Serial-JTAG port or the WebSocket.
 */
namespace console_uart {

inline bool accepts(const socket_message_Message &message) {
    switch (message.which_message) {
        case socket_message_Message_correlation_request_tag:
        case socket_message_Message_pingmsg_tag:
        case socket_message_Message_unsub_notif_tag: return true;
        case socket_message_Message_sub_notif_tag:
            return message.message.sub_notif.tag == socket_message_Message_wifi_status_tag;
        default: return false;
    }
}

} // namespace console_uart
