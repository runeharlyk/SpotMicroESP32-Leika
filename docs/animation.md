# Animations

An animation is a clip of keyframed body poses and leg targets that the robot plays as the ANIMATE mode.
The system is ported from the Hexapod and adapted to four legs.
`esp32/include/animation/animation.h` is the reference for every rule on this page; the app's TypeScript port is pinned to traces exported from it.

## Playing a clip

The Animations page lists the clips built into the firmware and the clips uploaded to the robot.
Each clip shows its description and a slider for every parameter it declares; Play sends `AnimationPlay` with those values, and Stop sends `AnimationStop`.
The robot must be powered (any mode but DEACTIVATED); a deactivated robot refuses the clip.

A clip runs through Entry, Playing, then Hold or Exit, and back to the mode it came from:

- Entry blends from the current pose to the first keyframe, in `entry_time` seconds (default 0.5); a foot that has to travel lifts on a step arc.
- Playing runs the keyframes on the clip clock, which the `SPEED` parameter scales; a `loop` clip repeats until stopped, and a non-looping clip plays `REPEAT` times.
- Hold freezes on the last keyframe until stopped, for a clip with `hold_end`.
- Exit blends back to stance in `exit_time` seconds (default 0.5).

A clip started from WALK returns to STAND; from any other mode it returns to that mode.
Choosing a mode while a clip plays runs its Exit first, then applies the mode; DEACTIVATED applies at once.
Starting a clip while another plays starts Entry from the current pose.

The robot reports `AnimationStatus` to subscribers: the clip, its state, its clip time and a mask of the legs the IK could not reach.

## The clip format

A clip is an `Animation` message from `platform_shared/animation.proto`, written as JSON in the protobuf JSON mapping and stored on the robot as the encoded message.
`animations/wave.json` is a complete example.

Units and frame:

- Lengths in mm, body angles in rad, joint angles in degrees, times in seconds.
- Offsets are relative to the standing posture, in REP-103: x forward, y left, z up.
- Positive roll lifts the left side, positive pitch lowers the nose, positive yaw turns left.
- Joint angles are in the IK output convention (coxa, femur, tibia), before the firmware's joint directions.

Fields:

- `name`: 1-32 characters of `[a-z0-9_-]`, unique on the robot and equal to the file stem.
- `description`: at most 96 bytes, shown in the app.
- `schema`: 1.
- `loop` and `hold_end`: at most one of the two.
- `entry_time`, `exit_time`: seconds, 0 for the default 0.5.
- `ride_height`: optional mm of body above the feet while the clip plays; absent keeps the height the body had at the start.
- `keyframes`: 1-32, the first at time 0 and times strictly increasing.
  Each has an `ease` (`LINEAR`, `EASE_IN`, `EASE_OUT`, `EASE_IN_OUT`) shaping the segment that ends at it, a `body` pose (absent is zero), and either no `legs` (every foot holds stance) or exactly four, in the order front-left, front-right, rear-left, rear-right.
  A leg is either a `foot` offset from its standing foot or `joints` angles; a leg may switch between the two from one keyframe to the next, and the blend stays continuous.
- `overlays`: up to 8 additive sines, each on one `body_axis` (0-5 in `BodyPose` field order) or one `foot_channel` (leg x 3 + axis, 0-11), with `amplitude`, `frequency` (Hz of clip time), `phase` (rad) and an active window `start` to `end` that ends no later than the last keyframe.
- `params`: up to 10 parameters the app exposes, each with an `id`, `min`, `default_value` and `max`.
  `SPEED` scales the clip clock and needs a positive `min`; `BODY_X` to `BODY_YAW` scale that body channel; `FOOT_LIFT` scales every foot's z offset; `OVERLAY_AMPLITUDE` scales every overlay; `REPEAT` sets the plays of a non-looping clip and needs `min` of at least 1.
  An undeclared parameter is 1.

The firmware validates a clip before it plays and refuses a broken one with the first error it finds.
It also sweeps the clip at its default parameters and reports the legs it could not reach; such a clip still plays, with those legs bent as far as they go.
The sweep only sees over-stretch: a pose that folds a leg into the body passes, since the joint model holds no joint limits yet.

## Built-in and uploaded clips

The built-in clips are the JSON files in `animations/`.
`esp32/scripts/pack_animations.py` packs them into `esp32/include/animation/builtin_clips.h` at build time, so every firmware image carries them, over the air too.
To add one, put a new `<name>.json` in `animations/` and rebuild; `animation_builtins_test` then checks it validates and stays reachable on every kinematics variant.

Uploaded clips live in `/animations/<name>.pb` on the robot's file system.
The Animations page uploads a `.json` clip, encoding it to the binary message first; it refuses a name that a built-in already uses, since the built-in wins.
A clip edited in the editor uploads the same way.

## The editor

Edit on a clip opens the editor: a timeline of keyframes, a pose panel for the selected keyframe's body and legs, a clip panel for the clip's own fields, overlays and parameters, and a 3D preview.
The preview is kinematic: it shows the poses the firmware would command, not whether the robot keeps its balance.

## Tests and parity

- `esp32/test/host/animation_test.cpp`, `animation_codec_test.cpp`, `animation_store_test.cpp`, `animation_validation_test.cpp` and `animation_builtins_test.cpp` test the firmware side on the host.
- `animations/fixtures/validation.json` holds clips with the error each must produce; the firmware and the app's validator both answer from it.
- `esp32/test/host/export_animation_traces.py` plays the built-in clips through the firmware code for every variant and writes `app/tests/fixtures/animation-trace-*.json`; `app/tests/unit/animation-player.spec.ts` pins the app's player to them.
  Regenerate and commit the traces when the animation code or a built-in clip changes.
