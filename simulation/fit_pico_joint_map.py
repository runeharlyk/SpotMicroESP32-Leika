"""Fits the Pico's firmware joint map for the web app's simulation (app/src/lib/simulation/robots.ts).

The firmware's joint angles are geometric, with a zero of their own; the Pico's MJCF joints are zero
at the CAD pose. Each MJCF joint is taken as sign * firmware angle + offset, and per leg the signs
(all 8 choices) and offsets are fitted so the MJCF foot sites follow the firmware's foot targets over
the MINI firmware trace (app/tests/fixtures), allowing a shift between the two body origins, which
the Pico's CAD and the firmware's MINI dimensions place differently.
Run from simulation/: uv run python fit_pico_joint_map.py
"""
import itertools
import json
import math
import os

import mujoco
import numpy as np
from scipy.optimize import least_squares

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
trace = json.load(open(f'{REPO}/app/tests/fixtures/firmware-trace-SPOTMICRO_ESP32_MINI.json'))
model = mujoco.MjModel.from_xml_path(f'{REPO}/simulation/src/resources/spot_pico/scene.xml')
data = mujoco.MjData(model)
DIR = [1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1]
LEGS = ['fl', 'fr', 'rl', 'rr']
JOINTS = ['hip', 'femur', 'tibia']
DEG = 0.0174532


def rot(o, p, y):
    cr, sr, cp, sp, cy, sy = math.cos(o), math.sin(o), math.cos(p), math.sin(p), math.cos(y), math.sin(y)
    return np.array([[cp * cy, -sy * cp, sp],
                     [sr * sp * cy + sy * cr, -sr * sp * sy + cr * cy, -sr * cp],
                     [sr * sy - sp * cr * cy, sr * cy + sp * sy * cr, cr * cp]])


ticks = trace['ticks'][::3]
samples = []
for t in ticks:
    b = t['body']
    R = rot(b['omega'] * DEG, b['phi'] * DEG, b['psi'] * DEG)
    centre = np.array([b['xm'], b['ym'], b['zm']])
    feet = [R.T @ (np.array(f) - centre) for f in b['feet']]
    directed = [a * d for a, d in zip(t['ik'], DIR)]
    samples.append((feet, directed))

qadr = {f'{l}_{j}': model.jnt_qposadr[mujoco.mj_name2id(model, 3, f'{l}_{j}_joint')] for l in LEGS for j in JOINTS}
site = {l: mujoco.mj_name2id(model, 6, f'foot_{l}') for l in LEGS}


def foot_mj(leg, q):
    data.qpos[:] = 0
    data.qpos[3] = 1
    for j, v in zip(JOINTS, q):
        data.qpos[qadr[f'{leg}_{j}']] = v
    mujoco.mj_kinematics(model, data)
    return data.site_xpos[site[leg]].copy()


def to_fw(p):
    """MJCF (x left, y back, z up) to the firmware's body frame (x forward, y up, z left)."""
    return np.array([-p[1], p[2], p[0]])


def fit_leg(li: int, leg: str):
    """The best of the 8 sign choices: (rms error m, signs, offsets rad, origin shift m)."""
    best = None
    for signs in itertools.product((1, -1), repeat=3):
        def residual(params):
            off, shift = params[:3], params[3:]
            res = []
            for feet, directed in samples:
                q = [s * math.radians(a) + o for s, a, o in zip(signs, directed[li * 3:li * 3 + 3], off)]
                res.extend(to_fw(foot_mj(leg, q)) + shift - feet[li])
            return res
        fit = least_squares(residual, np.zeros(6))
        rms = math.sqrt(np.mean(np.square(fit.fun)))
        if best is None or rms < best[0]:
            best = (rms, signs, fit.x[:3], fit.x[3:])
    return best


def main() -> None:
    signs, offsets = [], []
    for li, leg in enumerate(LEGS):
        rms, leg_signs, leg_offsets, shift = fit_leg(li, leg)
        print(f"{leg}: rms {rms * 1000:.2f} mm, origin shift {np.round(shift * 1000, 1)} mm")
        signs += leg_signs
        offsets += [round(math.degrees(o), 1) for o in leg_offsets]
    print("sign:", list(signs))
    print("offset (degrees):", offsets)


if __name__ == "__main__":
    main()
