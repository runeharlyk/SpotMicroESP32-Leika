#pragma once

// Copies a decoded animation.proto message into the plain Clip the evaluator reads, as on the Hexapod. Enum values
// are kept as integers so validate() reports an unknown ease or param id instead of the decoder.

#include <animation/animation.h>
#include <platform_shared/animation.pb.h>

namespace anim {

inline void fromProto(const animation_Animation &m, Clip &c) {
    c = Clip{};
    strncpy(c.name, m.name, NAME_LEN_MAX);
    strncpy(c.description, m.description, DESCRIPTION_LEN_MAX);
    c.schema = m.schema;
    c.loop = m.loop;
    c.holdEnd = m.hold_end;
    c.entryTime = m.entry_time;
    c.exitTime = m.exit_time;
    c.hasRideHeight = m.has_ride_height;
    c.rideHeight = m.ride_height;
    c.keyframeCount = m.keyframes_count;
    for (int i = 0; i < c.keyframeCount && i < KEYFRAME_MAX; ++i) {
        const animation_Keyframe &src = m.keyframes[i];
        Keyframe &k = c.keyframes[i];
        k.time = src.time;
        k.ease = (int)src.ease;
        k.body[ROLL] = src.body.roll;
        k.body[PITCH] = src.body.pitch;
        k.body[YAW] = src.body.yaw;
        k.body[X] = src.body.x;
        k.body[Y] = src.body.y;
        k.body[Z] = src.body.z;
        k.legCount = src.legs_count;
        for (int l = 0; l < k.legCount && l < LEGS; ++l) {
            const animation_LegTarget &lt = src.legs[l];
            LegTarget &t = k.legs[l];
            t.joints = lt.which_target == animation_LegTarget_joints_tag;
            if (t.joints) {
                t.v[0] = lt.target.joints.coxa;
                t.v[1] = lt.target.joints.femur;
                t.v[2] = lt.target.joints.tibia;
            } else {
                t.v[0] = lt.target.foot.x;
                t.v[1] = lt.target.foot.y;
                t.v[2] = lt.target.foot.z;
            }
        }
    }
    c.overlayCount = m.overlays_count;
    for (int i = 0; i < c.overlayCount && i < OVERLAY_MAX; ++i) {
        const animation_Overlay &src = m.overlays[i];
        Overlay &o = c.overlays[i];
        o.kind = src.which_channel == animation_Overlay_body_axis_tag   ? CHANNEL_BODY
                 : src.which_channel == animation_Overlay_foot_channel_tag ? CHANNEL_FOOT
                                                                           : CHANNEL_NONE;
        o.channel = o.kind == CHANNEL_BODY   ? (int)src.channel.body_axis
                    : o.kind == CHANNEL_FOOT ? (int)src.channel.foot_channel
                                             : 0;
        o.amplitude = src.amplitude;
        o.frequency = src.frequency;
        o.phase = src.phase;
        o.start = src.start;
        o.end = src.end;
    }
    c.paramCount = m.params_count;
    for (int i = 0; i < c.paramCount && i < PARAM_MAX; ++i) {
        c.params[i] = {(int)m.params[i].id, m.params[i].min, m.params[i].default_value, m.params[i].max};
    }
}

}  // namespace anim
