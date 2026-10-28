"""Per-episode heightfield ground for uneven-terrain training/eval.

Requires the hfield model variant (`scene_terrain.xml`). Every generator leaves a flat spawn disk at the origin so the
robot always spawns cleanly, and returns the ground's height there (m), which the robot's spawn height must add. The
policy is blind (IMU only, no exteroception), so terrain must stay gentle relative to spot_pico's ~64 mm body clearance
and 15 mm step height.

  bumps   smooth random bumps in [0, max_height] m
  slope   a plane tilted in a random direction, so the robot meets it uphill, downhill or across
  stairs  square terraces around the spawn, climbing up or down from it
"""

import mujoco
import numpy as np

FLAT_RADIUS = 0.15   # m, flat spawn disk around the origin (feet stance radius ~0.13 m)
RAMP = 0.10          # m, smoothstep ramp from flat disk to full terrain
COARSE_N = 33        # coarse noise grid -> ~0.06 m feature wavelength on a 2 m half-field


def _upsample_bilinear(coarse, n):
    k = coarse.shape[0]
    x = np.linspace(0.0, k - 1.0, n)
    xi = np.floor(x).astype(int)
    xf = (x - xi)[:, None]
    xi1 = np.minimum(xi + 1, k - 1)
    rows = coarse[xi] * (1.0 - xf) + coarse[xi1] * xf
    return rows[:, xi] * (1.0 - xf.T) + rows[:, xi1] * xf.T


def _field(model):
    """The 'terrain' hfield's id, its zmax (m), and the world x and y (m) of every sample: rows run along y."""
    hid = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_HFIELD, "terrain")
    nrow, ncol = int(model.hfield_nrow[hid]), int(model.hfield_ncol[hid])
    rx, ry, zmax, _ = model.hfield_size[hid]
    y, x = np.meshgrid(np.linspace(-ry, ry, nrow), np.linspace(-rx, rx, ncol), indexing="ij")
    return hid, zmax, x, y


def _spawn_blend(x, y):
    """0 on the spawn disk, rising by a smoothstep to 1 beyond it."""
    t = np.clip((np.hypot(x, y) - FLAT_RADIUS) / RAMP, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _write(model, hid, zmax, heights):
    assert heights.min() >= 0.0 and heights.max() <= zmax + 1e-9, f"terrain leaves the hfield's 0-{zmax} m"
    adr = model.hfield_adr[hid]
    model.hfield_data[adr:adr + heights.size] = (heights / zmax).ravel()
    nrow, ncol = heights.shape
    return float(heights[nrow // 2, ncol // 2])


def randomize_hfield(model, rng, max_height):
    """Fill the 'terrain' hfield with smooth random bumps of amplitude [0, max_height] m."""
    hid, zmax, x, y = _field(model)
    h = _upsample_bilinear(rng.uniform(0.0, 1.0, (COARSE_N, COARSE_N)), x.shape[0])
    return _write(model, hid, zmax, h * _spawn_blend(x, y) * max_height)


def flat_hfield(model):
    hid, zmax, x, _ = _field(model)
    return _write(model, hid, zmax, np.zeros_like(x))


def slope_hfield(model, rng, slope_deg):
    """A plane rising at `slope_deg` towards a random heading, through half the hfield's height at the origin and
    clipped to plateaus where it leaves the hfield."""
    hid, zmax, x, y = _field(model)
    heading = rng.uniform(0.0, 2.0 * np.pi)
    plane = np.tan(np.radians(slope_deg)) * (np.cos(heading) * x + np.sin(heading) * y)
    return _write(model, hid, zmax, np.clip(zmax / 2 + plane * _spawn_blend(x, y), 0.0, zmax))


def stairs_hfield(model, rng, riser, tread):
    """Square terraces of `riser` m height and `tread` m depth beyond the spawn disk, up from it or, at random, down."""
    hid, zmax, x, y = _field(model)
    steps = np.ceil(np.clip(np.maximum(np.abs(x), np.abs(y)) - FLAT_RADIUS, 0.0, None) / tread)
    top = min(zmax, riser * steps.max())
    h = np.minimum(riser * steps, top) if rng.random() < 0.5 else np.maximum(top - riser * steps, 0.0)
    return _write(model, hid, zmax, h)


def ground_height(model, x, y):
    """The hfield's height (m) at world (x, y), interpolated bilinearly between its samples."""
    hid = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_HFIELD, "terrain")
    nrow, ncol = int(model.hfield_nrow[hid]), int(model.hfield_ncol[hid])
    rx, ry, zmax, _ = model.hfield_size[hid]
    col = float(np.clip((x + rx) / (2 * rx) * (ncol - 1), 0.0, ncol - 1.0))
    row = float(np.clip((y + ry) / (2 * ry) * (nrow - 1), 0.0, nrow - 1.0))
    c0, r0 = min(int(col), ncol - 2), min(int(row), nrow - 2)
    fc, fr = col - c0, row - r0
    h = model.hfield_data[model.hfield_adr[hid]:model.hfield_adr[hid] + nrow * ncol].reshape(nrow, ncol)
    top = h[r0, c0] * (1 - fc) + h[r0, c0 + 1] * fc
    bottom = h[r0 + 1, c0] * (1 - fc) + h[r0 + 1, c0 + 1] * fc
    return float((top * (1 - fr) + bottom * fr) * zmax)
