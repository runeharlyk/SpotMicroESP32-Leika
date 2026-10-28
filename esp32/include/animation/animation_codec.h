#pragma once

// Copies a decoded animation.proto message into the plain Clip the evaluator reads, as on the Hexapod. Enum values
// are kept as integers so validate() reports an unknown ease or param id instead of the decoder.

#include <animation/animation.h>
#include <platform_shared/animation.pb.h>

namespace anim {

inline void bodyFromProto(const animation_BodyPose &src, float body[6]) {
    body[ROLL] = src.roll;
    body[PITCH] = src.pitch;
    body[YAW] = src.yaw;
    body[X] = src.x;
    body[Y] = src.y;
    body[Z] = src.z;
}

inline void legFromProto(const animation_LegTarget &src, LegTarget &t) {
    t.joints = src.which_target == animation_LegTarget_joints_tag;
    if (t.joints) {
        t.v[0] = src.target.joints.coxa;
        t.v[1] = src.target.joints.femur;
        t.v[2] = src.target.joints.tibia;
    } else {
        t.v[0] = src.target.foot.x;
        t.v[1] = src.target.foot.y;
        t.v[2] = src.target.foot.z;
    }
}

/** A pose to hold from its body and 0 or 4 legs (no legs: every foot in stance); the error, or nullptr. */
inline const char *poseFromProto(const animation_BodyPose &body, const animation_LegTarget *legs, int legCount,
                                 Pose &out) {
    if (legCount != 0 && legCount != LEGS) return "a pose has 0 or 4 legs";
    out = Pose {};
    bodyFromProto(body, out.body);
    for (int l = 0; l < legCount; ++l) legFromProto(legs[l], out.legs[l]);
    for (float v : out.body)
        if (!finite(v)) return "the pose's body must be finite";
    for (const LegTarget &leg : out.legs)
        for (float v : leg.v)
            if (!finite(v)) return "the pose's legs must be finite";
    return nullptr;
}

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
        bodyFromProto(src.body, k.body);
        k.legCount = src.legs_count;
        for (int l = 0; l < k.legCount && l < LEGS; ++l) legFromProto(src.legs[l], k.legs[l]);
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
