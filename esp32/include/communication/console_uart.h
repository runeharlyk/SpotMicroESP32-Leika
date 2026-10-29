#pragma once

#include <communication/topics.h>
#include <platform_shared/message.pb.h>

/**
 * What the serial link takes from a client on the console UART. That port runs at 115200 baud and carries the log as
 * well, too slow for streams, so it takes requests, pings and subscriptions to the state topics, which send a value
 * when it changes, such as the WiFi status; driving and streams need the USB-Serial-JTAG port or the WebSocket.
 */
namespace console_uart {

inline bool accepts(const socket_message_Message &message) {
    switch (message.which_message) {
        case socket_message_Message_correlation_request_tag:
        case socket_message_Message_pingmsg_tag:
        case socket_message_Message_unsub_notif_tag: return true;
        case socket_message_Message_sub_notif_tag:
            return isStateTopic(message.message.sub_notif.tag);
        default: return false;
    }
}

} // namespace console_uart
