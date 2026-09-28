# spot_pico simulation — MuJoCo residual-gait training

MuJoCo physics + Gymnasium + Stable-Baselines3 PPO for training a **residual walking policy**
on the `spot_pico` quadruped. The baseline is the ESP32 firmware gait ported to NumPy; the
policy learns only small per-foot corrections on top (`residual_pure`, à la spot_mini_mini D2 /
the Hexapod project). A zero action reproduces the firmware gait, so training starts from a
working gait and only learns stabilization.

Managed with **uv** (Python ≥ 3.13).

```bash
uv sync
```

## Architecture

```
command [vx, vy, yaw]  ->  analytic_gait_action  ->  firmware gait (phase clock, trot
                                                     offsets, stance + 12-pt Bezier swing)
   -> Cartesian foot targets (base frame)  + policy residual (12-D, ±15 mm)
   -> analytic IK  ->  12 joint angles  ->  MuJoCo position actuators (100 Hz control)
```

- `src/robot/firmware_gait.py` — NumPy port of `esp32/.../walk_state.h` + spot_pico analytic IK
  (verified to 0.0 mm against MuJoCo forward kinematics) + command→gait map.
- `src/sim/mj_runtime.py` — MuJoCo runtime wrapper (load model, IK→ctrl, IMU/contacts).
- `src/sim/domain_rand.py` — per-episode/step domain randomization for sim-to-real.
- `src/envs/quadruped_mj_env.py` — Gymnasium env; 12-D residual action, 38-D hardware-only
  observation (gravity-in-body, gyro, rpy, prev joint cmd, gait phase, command, prev action),
  ANYmal-style exponential velocity/yaw tracking reward.
- `src/resources/spot_pico/` — MJCF `scene.xml`, meshes, `linkage.py` (four-bar tibia map,
  deploy-time only), tuned `gait_coef.json`.
- `src/leika/` — high-level `Robot` façade (`stand`/`walk`/`rest`) over the sim.

The robot model comes from `spot_pico_description`. The four-bar tibia linkage (`linkage.py`)
is only needed when deploying to real servos; in sim the URDF tibia joint is actuated directly.

## Commands

```bash
uv run python -m src.robot.firmware_gait   # IK/FK self-test + stance angles
uv run python replay_gait.py --headless    # validate the zero-residual gait (all directions)
uv run python replay_gait.py --vx 0.05     # watch the baseline gait in the viewer

uv run python optimize_gait.py --iters 30  # calibrate command->gait map -> gait_coef.json

uv run python train_mj.py --smoke                                   # pipeline sanity run
uv run python train_mj.py --randomize --zero-final --init-std 0.3 \
    --curriculum --resample-steps 300 --timesteps 5_000_000 --num-envs 16

uv run python eval_policy.py runs/residual_pure_dr --vx 0.05        # watch a trained policy
uv run python visionary_demo.py                                     # high-level API demo

uv run pytest -q                                                    # regression tests
```

Uneven ground: pass `--terrain 0.008` to `train_mj.py` to train on the heightfield scene
(`scene_terrain.xml`) with per-episode random bumps up to the given height in metres. The policy
is blind (IMU only, no exteroception), so terrain only enters through proprioception.

TensorBoard: `uv run tensorboard --logdir runs`. Watch `terms/r_vel` (tracking) rise and
`terms/p_res` (residual magnitude) stay small — the policy should stabilize, not replace, the gait.

## Follow-ups (out of scope here)

- Model the servo PWM frame (50 Hz) in the action-latency randomization; the robot applies a new
  target at most every 20 ms, while the sim applies it every 10 ms.
- Hardware validation of IK joint-sign conventions and the four-bar linkage.

## Deploying to the robot

The spot_pico geometry is the Leika Mini, so a policy runs on firmware built with `SPOTMICRO_ESP32_MINI`, `USE_MPU6050=1` and `USE_POLICY=1` (see `esp32/features.ini`).

```bash
uv run python export_policy.py --run residual_pure_dr   # writes esp32/include/policy/leika_policy.h
uv run python export_golden.py                          # after changing the gait, IK, linkage or exporter
```

`export_policy.py` checks its numpy forward pass against SB3 before writing, bakes the command->gait coefficients recorded in the run's `gait_coef.json`, and embeds a golden input/output pair that the firmware verifies at boot.
Runs trained before `train_mj.py` recorded `gait_coef.json` need `--gait-coef` pointing at the coefficients they actually used.

On the robot, `WALK_NN` first settles into the trained stance, then runs the policy every 10 ms control tick from phase 0 with a zero action history, as the environment does after a reset.
It sends servo targets without the firmware's usual command smoothing, because the simulation has none.
`pio test -e native` replays `QuadrupedMjEnv.step` against the firmware's `WalkNNState` tick for tick, so the observation layout, the one-step-older joint and action history, the phase clock and the residual application stay in lockstep.

Still to verify on hardware before the first walk:

- the IMU mounting matrix `WalkNNState::IMU_FROM_BASE` (the bench test is described next to it);
- servo direction and zero calibration, now that Mini joint angles are 0 at the CAD stance pose;
- the joystick turn direction in `WalkNNState::handleCommand`.
