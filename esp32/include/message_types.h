#pragma once

#include <platform_shared/message.pb.h>

struct CommandMsg {
    float lx, ly, rx, ry, h, s, s1;

    void fromProto(const socket_message_HumanInputData& data) {
        lx = data.has_left ? data.left.x : 0;
        ly = data.has_left ? data.left.y : 0;
        rx = data.has_right ? data.right.x : 0;
        ry = data.has_right ? data.right.y : 0;
        h = data.height;
        s = data.speed;
        s1 = data.s1;
    }
};