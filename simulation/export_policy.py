"""Export a trained SB3 actor + VecNormalize stats to a self-contained C++ header for the ESP32.

The header carries the MLP weights, the observation normalization, the command->gait coefficients
the run was trained on, the residual and command scaling, a golden TEST_OBS/TEST_ACT pair for a
boot-time self-check, and an inline `leika_policy::infer(obs, act)` (plain float math, no
TFLite/ESP-DL; a ~52k-parameter MLP does not need them). Adapted from the Hexapod project's exporter.

  uv run python export_policy.py --run residual_pure_dr
  uv run python export_policy.py --model runs/x/final_model.zip --vecnorm runs/x/vecnormalize.pkl \
      --gait-coef runs/x/gait_coef.json

Before writing, the numpy forward pass is checked against SB3 (must match to ~1e-6).
"""

import argparse
import json
import os

import numpy as np
import torch
from stable_baselines3 import PPO
from stable_baselines3.common.vec_env import DummyVecEnv, VecNormalize

from src.envs.quadruped_mj_env import make_env, FOOT_RESIDUAL, CMD_VX, CMD_VY, CMD_YAW
from src.sim.mj_runtime import CONTROL_DT

GAIT_COEF_KEYS = ("gain_x", "gain_y", "gain_yaw", "speed_base", "speed_slope", "step_height", "step_depth")
DEFAULT_OUT = os.path.join(os.path.dirname(__file__), "..", "esp32", "include", "policy", "leika_policy.h")

OBS_LAYOUT = """\
// Observation layout (OBS_DIM = 38 floats, raw units; normalization happens inside infer()).
// All vectors are in the spot_pico base frame: +X = left, +Y = rear, +Z = up.
//   [0:3]   gravity in the body frame, world-DOWN convention: level = (0, 0, -1)
//   [3:6]   gyro (rad/s), body frame
//   [6:9]   roll, pitch, yaw (rad) of the base, yaw relative to where the episode started
//   [9:21]  previous commanded joint angles (rad, 0 = CAD stance), legs fr, fl, rr, rl x (hip, femur, tibia)
//   [21:23] gait phase clock [sin(2 pi phase), cos(2 pi phase)]
//   [23:26] command [vx forward (m/s), vy left (m/s), yaw rate CCW (rad/s)]
//   [26:38] previous action (this function's previous act_out)
// Action layout (ACT_DIM = 12): per-leg foot residuals [dx, dy, dz] * FOOT_RESIDUAL (m), legs fr, fl, rr, rl,
// added to spot_pico::generateFeet() before IK. A zero action reproduces the baseline gait exactly.
"""


def extract(model):
    """[(W, b), ...] for the hidden layers (tanh after each) and the final linear layer."""
    layers = []
    for module in model.policy.mlp_extractor.policy_net:
        if isinstance(module, torch.nn.Linear):
            layers.append((module.weight.detach().cpu().numpy(), module.bias.detach().cpu().numpy()))
        elif not isinstance(module, torch.nn.Tanh):
            raise SystemExit(f"unsupported actor layer {type(module).__name__}: infer() only implements "
                             "Linear + Tanh")
    layers.append((model.policy.action_net.weight.detach().cpu().numpy(),
                   model.policy.action_net.bias.detach().cpu().numpy()))
    return layers


def np_infer(obs_raw, layers, mean, var, clip, eps, clip_action=True):
    x = np.clip((obs_raw - mean) / np.sqrt(var + eps), -clip, clip)
    for i, (W, b) in enumerate(layers):
        x = W @ x + b
        if i < len(layers) - 1:
            x = np.tanh(x)
    return np.clip(x, -1.0, 1.0) if clip_action else x


def carr(name, values):
    flat = np.asarray(values, dtype=np.float64).ravel()
    return f"static const float {name}[{flat.size}] = {{{', '.join(f'{v:.8e}f' for v in flat)}}};\n"


INFER_CPP = """\
// NOT reentrant (static buffers keep the control-task stack small); call from the control task only.
inline void infer(const float *obs_raw, float *act_out) {
    static float cur[MAX_WIDTH], nxt[MAX_WIDTH];
    for (int i = 0; i < OBS_DIM; ++i) {
        float v = (obs_raw[i] - OBS_MEAN[i]) / sqrtf(OBS_VAR[i] + OBS_EPS);
        cur[i] = v > OBS_CLIP ? OBS_CLIP : (v < -OBS_CLIP ? -OBS_CLIP : v);
    }
    int in_dim = OBS_DIM;
    for (int l = 0; l < N_LAYERS; ++l) {
        const float *W = WEIGHTS[l];
        const float *B = BIASES[l];
        for (int o = 0; o < LAYER_OUT[l]; ++o) {
            float acc = B[o];
            const float *row = W + o * in_dim;
            for (int j = 0; j < in_dim; ++j) acc += row[j] * cur[j];
            nxt[o] = l < N_LAYERS - 1 ? tanhf(acc) : acc;
        }
        in_dim = LAYER_OUT[l];
        for (int o = 0; o < in_dim; ++o) cur[o] = nxt[o];
    }
    for (int o = 0; o < ACT_DIM; ++o) act_out[o] = cur[o] > 1.f ? 1.f : (cur[o] < -1.f ? -1.f : cur[o]);
}

// Boot-time self-check: max |infer(TEST_OBS) - TEST_ACT| (expect < 1e-4).
inline float selfCheck() {
    float act[ACT_DIM];
    infer(TEST_OBS, act);
    float err = 0.f;
    for (int i = 0; i < ACT_DIM; ++i) err = fmaxf(err, fabsf(act[i] - TEST_ACT[i]));
    return err;
}
"""


def render_header(layers, mean, var, clip, eps, coef, test_obs, test_act, source):
    obs_dim, act_dim = layers[0][0].shape[1], layers[-1][0].shape[0]
    widths = [obs_dim] + [W.shape[0] for W, _ in layers]
    parts = [
        "#pragma once\n",
        "// Auto-generated by simulation/export_policy.py - DO NOT EDIT.\n",
        f"// Source: {source}\n",
        OBS_LAYOUT,
        "\n#include <math.h>\n#include <spot_pico/residual_gait.h>\n\nnamespace leika_policy {\n\n",
        f"constexpr int OBS_DIM = {obs_dim};\nconstexpr int ACT_DIM = {act_dim};\n",
        f"constexpr int N_LAYERS = {len(layers)};\nconstexpr int MAX_WIDTH = {max(widths)};\n",
        f"constexpr float OBS_CLIP = {clip:.6f}f;\nconstexpr float OBS_EPS = {eps:.3e}f;\n\n",
        f"constexpr float CONTROL_DT = {CONTROL_DT}f;  // s; the policy and the gait phase run at this rate\n",
        f"constexpr float FOOT_RESIDUAL = {FOOT_RESIDUAL}f;  // m of foot correction per unit action\n",
        f"constexpr float CMD_VX_MIN = {CMD_VX[0]}f, CMD_VX_MAX = {CMD_VX[1]}f;  // m/s\n",
        f"constexpr float CMD_VY_MAX = {CMD_VY[1]}f;  // m/s\n",
        f"constexpr float CMD_YAW_MAX = {CMD_YAW[1]}f;  // rad/s\n",
        "// Command -> gait coefficients this run was trained with.\n",
        "constexpr spot_pico::GaitCoef GAIT_COEF = {"
        + ", ".join(f"{float(coef[k]):.8e}f" for k in GAIT_COEF_KEYS) + "};\n\n",
        carr("OBS_MEAN", mean),
        carr("OBS_VAR", var),
        f"static const int LAYER_OUT[{len(layers)}] = {{{', '.join(str(W.shape[0]) for W, _ in layers)}}};\n",
    ]
    for i, (W, b) in enumerate(layers):
        parts += [carr(f"WEIGHT{i}", W), carr(f"BIAS{i}", b)]
    parts += [
        f"static const float *const WEIGHTS[{len(layers)}] = {{"
        + ", ".join(f"WEIGHT{i}" for i in range(len(layers))) + "};\n",
        f"static const float *const BIASES[{len(layers)}] = {{"
        + ", ".join(f"BIAS{i}" for i in range(len(layers))) + "};\n\n",
        carr("TEST_OBS", test_obs),
        carr("TEST_ACT", test_act),
        "\n",
        INFER_CPP,
        "\n} // namespace leika_policy\n",
    ]
    return "".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", default=None, help="run dir under ./runs (sets --model, --vecnorm, --gait-coef)")
    ap.add_argument("--model", default=None)
    ap.add_argument("--vecnorm", default=None)
    ap.add_argument("--gait-coef", default=None, help="gait_coef.json the run was trained with")
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()
    if args.run:
        rundir = os.path.join("runs", args.run)
        args.model = args.model or os.path.join(rundir, "final_model.zip")
        args.vecnorm = args.vecnorm or os.path.join(rundir, "vecnormalize.pkl")
        args.gait_coef = args.gait_coef or os.path.join(rundir, "gait_coef.json")
    if not (args.model and args.vecnorm and args.gait_coef):
        raise SystemExit("pass --run <name>, or all of --model, --vecnorm and --gait-coef")
    if not os.path.exists(args.gait_coef):
        raise SystemExit(f"{args.gait_coef} not found. Runs trained before train_mj.py recorded it need "
                         "--gait-coef pointing at the coefficients actually used; baking the wrong ones "
                         "breaks 'zero action = baseline gait' on the robot.")
    with open(args.gait_coef) as f:
        coef = json.load(f)

    model = PPO.load(args.model, device="cpu")
    venv = VecNormalize.load(args.vecnorm, DummyVecEnv([make_env()]))
    mean = venv.obs_rms.mean.astype(np.float64)
    var = venv.obs_rms.var.astype(np.float64)
    clip, eps = float(venv.clip_obs), float(venv.epsilon)
    layers = extract(model)

    rng = np.random.default_rng(0)
    max_err = 0.0
    for _ in range(200):
        raw = rng.normal(mean, np.sqrt(var) + 1e-3).astype(np.float32)
        with torch.no_grad():
            sb3 = model.policy.get_distribution(
                torch.as_tensor(venv.normalize_obs(raw)).float().unsqueeze(0)).distribution.mean.numpy()[0]
        npy = np_infer(raw.astype(np.float64), layers, mean, var, clip, eps, clip_action=False)
        max_err = max(max_err, float(np.max(np.abs(npy - sb3))))
    print(f"numpy-vs-SB3 max error: {max_err:.2e}")
    if max_err >= 1e-4:
        raise SystemExit("numpy forward pass disagrees with SB3; refusing to export")

    test_obs = rng.normal(mean, np.sqrt(var) + 1e-3).astype(np.float32)
    test_act = np_infer(test_obs.astype(np.float64), layers, mean, var, clip, eps).astype(np.float32)
    source = os.path.relpath(args.model).replace(os.sep, "/")
    header = render_header(layers, mean, var, clip, eps, coef, test_obs, test_act, source)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    n_params = sum(W.size + b.size for W, b in layers)
    print(f"wrote {os.path.abspath(args.out)} (obs={layers[0][0].shape[1]}, act={layers[-1][0].shape[0]}, "
          f"{n_params:,} params, ~{n_params * 4 / 1024:.0f} KB fp32)")


if __name__ == "__main__":
    main()
