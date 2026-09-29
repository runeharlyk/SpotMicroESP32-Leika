# Robot selection in the simulation - design

Date: 2026-09-29.
Status: draft for review.

## Goal

The Simulation view lets the user choose which robot to simulate: Pico, Spot Micro or Yertle.
Each robot walks under the controller its real counterpart runs, so the simulation shows what the firmware actually does on that body.
For the Pico, the user can also switch to the training controller (the port of `firmware_gait.py` shipped in the first simulation view), so the two can be compared side by side on the same body.
Success: each of the three robots stands for 2 s and walks forward under Walk + left stick in the headless test and in the browser, and the choice persists across reloads.

Out of scope: running the trained policy, tuning the Spot Micro or Yertle physics against real hardware, and replacing the 3D view's own gait and kinematics with the new firmware port (a natural follow-up, listed at the end).

## Findings that shape the design

- The firmware's motion code (`esp32/include/motion_states/*.h`, `kinematics.h`, `utils/math_utils.h`, about 630 lines) depends on the ESP-IDF only through two `ESP_LOGI` calls and one `dspm_mult_f32_ae32` matrix multiply; nanopb's generated messages compile on a host.
  It can therefore be compiled on a PC with two stub headers, which also gives the firmware its first host test harness.
- The kinematics variant is chosen at compile time (`SPOTMICRO_ESP32`, `SPOTMICRO_ESP32_MINI`, `SPOTMICRO_YERTLE`).
- The Pico has a complete MJCF; Spot Micro (`app/static/spot_micro.urdf.xacro`) and Yertle (`app/static/yertle.URDF`) exist only as URDF, with incomplete collision and inertia data (Spot Micro: 7 inertials, 7 collision shapes; Yertle: 18 and 9) and no actuators, foot contacts or sensors.
- The models and the firmware disagree on leg lengths for Spot Micro (xacro femur 0.1075 / tibia 0.130 against firmware 0.1112 / 0.1185) as they do for the Pico; Yertle's URDF matches the firmware's femur, tibia and hip spacing to 1 mm, but its coxa offset is 0.027 against the firmware's 0.035.
- The app's own `gait.ts` and `kinematic.ts` are hand ports that have drifted from the firmware (see the audit), so they are not a trustworthy controller.
- The 3D view already maps firmware-convention angles onto the Spot Micro and Yertle URDF joints (joint order and the `dir` sign table in `Visualization.svelte`), which is the mapping those two robots need.

## Decisions

1. Firmware controller: a TypeScript port of `walk_state.h`, `stand_state.h`, `rest_state.h` and `kinematics.h`, one class parameterised by the variant's `KinConfig`, pinned per variant by golden traces from the real headers compiled on the host.
2. Host harness: `esp32/test/host/` with the stub headers and a trace program, built with the host C++ compiler by a script; its traces are committed as fixtures like the Pico trace.
3. MJCF for Spot Micro and Yertle: generated from their URDFs by a Python script in `simulation/` (MuJoCo loads URDF natively; the script adds a floor, a foot sphere per leg at the toe frames, position actuators and the IMU site), committed as generated files next to the Pico scene, with a test that regenerating produces the same files.
   Their inertia is taken from the URDF as is, which the view states ("physics approximate").
4. One simulation class for every robot, fed by a robot definition: MJCF paths, mesh zip, joint names in actuator order, the controller, and the mapping from controller angles to joint targets (order, sign, zero offset).
5. Controllers per robot: Pico offers "Firmware (MINI)" and "Training (Python)"; Spot Micro and Yertle offer "Firmware".
   Default robot: the connected robot's variant when it is one of the three, else the Pico; the choice persists in the browser.
6. The robot and controller choosers sit in the Simulation view's corner; changing either rebuilds the simulation from a standing start.

## Joint mapping

- Spot Micro and Yertle: the joint order and signs the 3D view uses today, verified by the headless stand test (a wrong sign folds a leg and the robot falls).
- Pico with the firmware controller: the MINI angles are geometric angles in the firmware's convention; the zero offsets onto the MJCF joints (whose zero is the CAD stance) are derived from the firmware's own forward kinematics at its default stance and checked by an FK round trip against the MJCF foot sites.
  Where the MINI geometry and the MJCF disagree, the feet land where the firmware puts them on the Pico's real body, which is the point of offering both controllers.

## Testing

- Per variant, the TypeScript firmware port against the host trace to 1e-5 (the firmware computes in `float`, so the tolerance is float precision, not 1e-9).
- Per robot, headless physics: stands for 2 s, walks forward for 3 s, no fall; for the Pico, with both controllers.
- The MJCF generator's regeneration test in `simulation/tests`.
- Browser: switching robot and controller in the view, reload keeps the choice, revisit leaves one canvas.

## Open risks

- Spot Micro's URDF has too little collision and inertia data for convincing physics; the generated MJCF may need hand-set foot geometry and masses to stand at all, and those numbers would be guesses until measured.
- The MINI-to-MJCF zero offsets could turn out ill-defined if the two geometries differ too much; the fallback is to offer only the training controller on the Pico, as today.
- The host harness compiles the firmware with a different compiler and `float` behaviour than the ESP32's; traces pin the logic, not bit-exact on-device results.

## Follow-up (not in this spec)

- Replace the 3D view's `gait.ts` and `kinematic.ts` with the pinned firmware port, removing the drift the audit found.
