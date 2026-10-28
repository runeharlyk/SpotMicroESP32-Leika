"""System identification: fits the simulation's servo model, actuator lag and IMU noise to a telemetry recording.

What a recording can determine, and from what:
  - Gyro bias and IMU noise: from still segments, where the written joint angles do not change, so the gyro's mean is
    its bias (after the boot calibration) and the spread of gyro and roll/pitch their noise. The roll/pitch bias is
    not identifiable: on a still robot a mounting tilt and a tilted floor look alike.
  - Sensor-to-servo latency: from the ticks' timing fields (telemetry_stats.summarize).
  - The servo supply voltage, one stall strength common to all twelve servos, and the actuator lag (from the servo
    write until the servo acts on it, the wait for the next PWM frame included): by replaying the written joint angles
    through the MuJoCo Pico model and matching the body's recorded gyro and gravity direction. The robot has no servo
    feedback, so the IMU is all that constrains the dynamics. Per-servo strengths, masses, friction and the position
    loop's gain are not identified and stay nominal, so the fit absorbs their errors.

The replay starts from a robot standing still, so a recording must begin with one, and it stops at the first lost
tick, after which the written angles are unknown. The cost is the noise-scaled misfit of gyro and gravity direction,
each channel's mean removed (a gyro bias or a mounting tilt). It is a mosaic: a footfall that lands a physics step
earlier or later changes it by up to hundreds of noise variances, so it splits into cells a few hundredths of a volt
wide, scattered along the valley where a lower supply and a stronger servo trade off. A coarse grid finds the lag, a
finer one the basins at it, a simplex refines the lowest few, and the lag is searched in whole 2 ms physics steps.
The servo parameters' uncertainty is how far the cells the data cannot rule out reach around the optimum
(_servo_sigma), the lag's the profile of the cost over the neighbouring steps, both scaled by the residual variance.
They assume white residuals, so on real data, where the model's errors are correlated in time, they are lower bounds.
A parameter whose 2-sigma interval would span more than half of today's randomisation range is reported as not
identifiable instead of with a value, and so are both servo parameters when the cells the data allows reach that far
along either.
"""
from __future__ import annotations

from dataclasses import dataclass, replace

import mujoco
import numpy as np
from scipy.optimize import minimize

from src.sim import domain_rand
from src.sim.mj_runtime import SpotPicoSim
from src.sim.recording import Recording, to_model_frame, to_model_joints
from src.sim.servo import RATED_VOLTS
from src.sim.telemetry_stats import summarize

PICO_VARIANT = "SPOTMICRO_ESP32_MINI"
STILL_RAD = 1e-4          # a written angle changing by more than this in a tick is a move
STILL_MARGIN_S = 0.5      # the body settles for this long after a move
MIN_STILL_TICKS = 100     # IMU samples a noise estimate needs
SETTLE_S = 1.0            # the replay's first second, while the model settles onto its feet, is not compared
GYRO_VALID, ORIENTATION_VALID = 2, 8


@dataclass(frozen=True)
class Parameter:
    name: str
    unit: str
    bounds: tuple[float, float]  # where the fit searches
    prior: tuple[float, float]   # today's randomisation range, which an identified value must narrow

    def value(self, unit_value):
        return self.bounds[0] + unit_value * (self.bounds[1] - self.bounds[0])


_LATENCY_STEPS = domain_rand.ACTION_LATENCY_STEPS
SERVO_PARAMETERS = (
    Parameter("supply_volts", "V", RATED_VOLTS, domain_rand.SUPPLY_VOLTS),
    Parameter("stall_strength", "", (0.7, 1.3), domain_rand.STALL_SPREAD),
)
# The lag is searched in whole physics steps: the model resolves a footfall to one step, so a lag between two steps
# only blurs the command, which moves the footfalls as much as a different servo would. The search spans twice the
# 20 ms frame of the servos' 50 Hz PWM, which alone delays a written angle by up to one frame.
LAG = Parameter("actuator_lag_ms", "ms", (0.0, 40.0),
                (_LATENCY_STEPS[0] * domain_rand.CONTROL_DT * 1e3, (_LATENCY_STEPS[1] - 1) * domain_rand.CONTROL_DT * 1e3))
SERVO_GRID = 4             # coarse search points per servo parameter, for the lag
# At the lag, the cost has several basins along the valley where supply and strength trade off, so a finer grid finds
# them and a simplex starts in each of the lowest. Its points avoid the round values a synthetic truth tends to sit on.
BASIN_GRID = 12
BASINS = 3
SCAN_SPACING = 1.0 / 120   # unit values: 0.01 V and 0.005 of strength, about a cell of the cost's mosaic
ALLOWED_CHI2 = 4.0         # a cell this close above the best is within a sigma of beating it (see _servo_sigma)
LAG_GRID_STRIDE = 3        # the coarse search tries every third lag step
MISFIT_RMS = 2.0           # a fit residual this many noise sigmas means the model misses the recording


@dataclass(frozen=True)
class Estimate:
    value: float | None
    std: float | None = None  # one sigma; None where the value is an empirical percentile
    unit: str = ""
    note: str = ""            # why it is not identifiable, or what qualifies it

    @property
    def identified(self) -> bool:
        return self.value is not None


def _not_identifiable(reason: str, unit: str = "") -> Estimate:
    return Estimate(None, unit=unit, note=reason)


def still_ticks(rec: Recording) -> np.ndarray:
    """Ticks with no written angle changing within STILL_MARGIN_S either side, and the dead-man stop not in force."""
    t = rec.ticks
    moves = np.r_[False, np.any(np.abs(np.diff(t["angles"], axis=0)) > STILL_RAD, axis=1)]
    margin = int(round(STILL_MARGIN_S / domain_rand.CONTROL_DT))
    near_move = np.convolve(moves.astype(float), np.ones(2 * margin + 1), "same") > 0
    return ~near_move & ~t["link_lost"].astype(bool)


def imu_estimates(rec: Recording) -> dict[str, Estimate]:
    still = still_ticks(rec)
    imu = still & ((rec.ticks["imu_valid"] & GYRO_VALID) != 0)
    n = int(np.sum(imu))
    names = [f"gyro_bias_{axis}" for axis in "xyz"] + ["gyro_noise_std", "rpy_noise_deg"]
    if n < MIN_STILL_TICKS:
        reason = f"{n} still IMU samples, {MIN_STILL_TICKS} needed"
        return {name: _not_identifiable(reason) for name in names}
    stats = summarize(replace(rec, ticks={name: values[still] for name, values in rec.ticks.items()}))
    noise = stats["gyro_noise_std"]
    estimates = {f"gyro_bias_{axis}": Estimate(stats[f"gyro_bias_{axis}"], noise / np.sqrt(n), "rad/s")
                 for axis in "xyz"}
    estimates["gyro_noise_std"] = Estimate(noise, noise / np.sqrt(2 * n), "rad/s")
    estimates["rpy_noise_deg"] = Estimate(stats["rpy_noise_deg"], stats["rpy_noise_deg"] / np.sqrt(2 * n), "deg")
    estimates["rpy_bias"] = _not_identifiable("a mounting tilt and a tilted floor look alike on a still robot")
    return estimates


def latency_estimates(rec: Recording) -> dict[str, Estimate]:
    stats = summarize(rec)
    if "sensor_to_servo_ms_p50" not in stats:
        return {"sensor_to_servo_ms": _not_identifiable("no tick carries an IMU sample")}
    return {name: Estimate(stats[name], unit="ms", note="empirical percentile")
            for name in ("sensor_to_servo_ms_p50", "sensor_to_servo_ms_p99")}


class Replay:
    """The recording's written joint angles replayed through the Pico model; `imu(params)` gives the model's gyro and
    gravity direction (model frame) at the recorded IMU sample times."""

    def __init__(self, rec: Recording, ticks: int):
        t = {name: values[:ticks] for name, values in rec.ticks.items()}
        origin = t["t_us"][0]
        self.sim = SpotPicoSim()
        self.h = self.sim.model.opt.timestep
        self.ctrl = to_model_joints(t["angles"])
        written_s = (t["t_us"] + t["compute_us"] + t["servo_write_us"] - origin) / 1e6
        # The servos hold each written angle until the next. The running integral of that is exact under linear
        # interpolation, and gives each physics step the mean of the delayed command over its span, which is exact
        # whenever the command does not change inside a step.
        knots = np.r_[-1.0, written_s]
        held = np.vstack([self.ctrl[:1], self.ctrl])
        self.knots = np.r_[knots, knots[-1] + 1.0]
        self.integral = np.vstack([np.zeros(12), np.cumsum(held * np.diff(self.knots)[:, None], axis=0)])
        sample_s = (t["imu_t_us"].astype(np.int64) - np.int64(origin)) / 1e6
        valid = (t["imu_valid"] & (GYRO_VALID | ORIENTATION_VALID)) == (GYRO_VALID | ORIENTATION_VALID)
        self.used = valid & (sample_s >= SETTLE_S)
        self.sample_s = sample_s[self.used]
        self.steps = int(np.ceil(self.sample_s.max() / self.h)) + 1 if self.used.any() else 0
        self.measured = np.hstack([to_model_frame(t["gyro"][self.used]),
                                   to_model_frame(t["gravity"][self.used])[:, :2]])

    def _start(self, volts: float, strength: float) -> None:
        sim = self.sim
        sim.reset_to_stand()
        sim.servo.set_supply(volts, strength)
        sim.data.qpos[sim.qpos_adr] = self.ctrl[0]
        mujoco.mj_kinematics(sim.model, sim.data)
        foot_radius = sim.model.geom_size[sim.foot_geom_ids[0], 0]
        sim.data.qpos[sim.base_qposadr + 2] += foot_radius - sim.data.site_xpos[sim.foot_site_ids, 2].min()
        mujoco.mj_forward(sim.model, sim.data)

    def imu(self, volts: float, strength: float, lag_ms: float) -> np.ndarray:
        sim = self.sim
        self._start(volts, strength)
        edges = np.arange(self.steps + 1) * self.h - lag_ms / 1e3
        integral = np.column_stack([np.interp(edges, self.knots, self.integral[:, j]) for j in range(12)])
        commands = np.diff(integral, axis=0) / self.h
        signals = np.empty((self.steps + 1, 5))
        signals[0] = 0.0
        for step, command in enumerate(commands, start=1):
            sim.data.ctrl[sim.act_ids] = command
            mujoco.mj_step(sim.model, sim.data)
            signals[step, :3] = sim.gyro()
            signals[step, 3:] = -sim.data.xmat[sim.base_id, 6:8]  # gravity in the base frame: -R^T z
        times = np.arange(self.steps + 1) * self.h
        return np.column_stack([np.interp(self.sample_s, times, signals[:, c]) for c in range(5)])


@dataclass
class _Fit:
    cost: float
    servo: np.ndarray  # unit values of SERVO_PARAMETERS


class _Search:
    """The noise-scaled residuals as a function of the servo parameters (unit values) and the lag step."""

    def __init__(self, replay: Replay, scale: np.ndarray):
        self.replay, self.scale = replay, scale
        self.lags_ms = np.arange(0.0, LAG.bounds[1] + 1e-9, replay.h * 1e3)
        self.by_lag: dict[int, _Fit] = {}

    def residuals(self, servo, lag: int) -> np.ndarray:
        volts, strength = (p.value(u) for p, u in zip(SERVO_PARAMETERS, np.clip(servo, 0.0, 1.0)))
        error = self.replay.imu(volts, strength, self.lags_ms[lag]) - self.replay.measured
        # Each channel's mean is a gyro bias or a mounting tilt, not dynamics.
        return ((error - error.mean(axis=0)) / self.scale).ravel()

    def cost(self, servo, lag: int) -> float:
        return float(np.sum(self.residuals(servo, lag) ** 2))

    def _simplex(self, lag: int, start: np.ndarray) -> _Fit:
        spacing = 1.0 / (BASIN_GRID - 1)
        simplex = np.vstack([start, start + np.diag(np.where(start + spacing > 1.0, -spacing, spacing))])
        result = minimize(self.cost, start, args=(lag,), method="Nelder-Mead", bounds=[(0.0, 1.0)] * 2,
                          options={"initial_simplex": simplex, "xatol": 1e-3, "fatol": 1e-2})
        return _Fit(float(result.fun), result.x)

    def refine(self, lag: int, start: np.ndarray) -> _Fit:
        """The simplex's best servo parameters at one lag, refined once per lag."""
        if lag not in self.by_lag:
            self.by_lag[lag] = self._simplex(lag, start)
        return self.by_lag[lag]

    def basins(self, lag: int) -> _Fit:
        """The best of the simplexes started in the BASINS lowest local minima of a BASIN_GRID grid at the lag."""
        axis = np.linspace(0.0, 1.0, BASIN_GRID)
        costs = np.array([[self.cost(np.array([v, s]), lag) for s in axis] for v in axis])
        padded = np.pad(costs, 1, constant_values=np.inf)
        minima = sorted((costs[i, j], i, j) for i in range(BASIN_GRID) for j in range(BASIN_GRID)
                        if costs[i, j] <= padded[i:i + 3, j:j + 3].min())
        fits = sorted((self._simplex(lag, np.array([axis[i], axis[j]])) for _, i, j in minima[:BASINS]),
                      key=lambda fit: fit.cost)
        self.basin_lag, self.by_lag, self.rivals = lag, {lag: fits[0]}, fits[1:]
        return fits[0]

    def run(self) -> int:
        """A coarse grid for the lag, the basins at its best lag, then the simplex at each next lag step while that
        lowers the cost; the basins again wherever that settles, until it settles where they were searched. Returns
        the best lag step."""
        axis = np.linspace(0.0, 1.0, SERVO_GRID)
        grid = [(np.array([v, s]), lag) for v in axis for s in axis
                for lag in range(0, len(self.lags_ms), LAG_GRID_STRIDE)]
        _, best = min(grid, key=lambda point: self.cost(*point))
        searched = set()
        while best not in searched:
            searched.add(best)
            self.basins(best)
            for direction in (-1, 1):
                while 0 <= best + direction < len(self.lags_ms):
                    here = self.by_lag[best]
                    if self.refine(best + direction, here.servo).cost >= here.cost:
                        break
                    best += direction
        return best


def _servo_sigma(search: _Search, lag: int):
    """The servo parameters' sigmas (unit values) at the lag, from the points around the optimum the data cannot rule
    out. The cost is a mosaic of cells a few hundredths of a volt wide along the valley where supply and strength trade
    off, so neither a Jacobian nor a curvature sees its basin, and the cells the data allows need not touch the best
    one. Two cells whose noise-free costs differ by d (in chi-square units, the residual variance) differ in a
    recording by d plus noise of sigma 2 sqrt(d), so a cell less than ALLOWED_CHI2 = 4 above the best is within a sigma
    of beating it. Each parameter's sigma is how far such points reach along it, among the rival basins and a grid
    SCAN_SPACING apart (about a cell) out to just past where the sigma would make it unidentifiable; for a smooth bowl
    that is twice the sigma. A point lower than the optimum by more than the residual variance moves the optimum
    there. The grid resolves the sigma to half its spacing. Also returns which parameters' allowed points reach the
    grid's edge: there the region is cut off, and with it the other parameter's sigma."""
    widths = np.array([p.bounds[1] - p.bounds[0] for p in SERVO_PARAMETERS])
    unidentifiable = np.array([(p.prior[1] - p.prior[0]) / 8 for p in SERVO_PARAMETERS]) / widths  # see _estimate
    reach = np.round(unidentifiable / SCAN_SPACING) + 1  # one step past it, so a sigma reaching the edge refuses
    offsets = SCAN_SPACING * np.stack(np.meshgrid(*(np.arange(-n, n + 1) for n in reach), indexing="ij"), axis=-1)
    while True:
        fit = search.by_lag[lag]
        grid = fit.servo + offsets.reshape(-1, 2)
        points = np.vstack([grid[np.all((grid >= 0.0) & (grid <= 1.0), axis=1)],
                            np.reshape([rival.servo for rival in search.rivals], (-1, 2))])
        costs = np.array([search.cost(point, lag) for point in points])
        residual = search.residuals(fit.servo, lag)
        residual_variance = residual @ residual / max(len(residual) - len(SERVO_PARAMETERS) - 1, 1)
        if costs.min() < fit.cost - residual_variance:
            search.by_lag[lag] = search._simplex(lag, points[costs.argmin()])
            continue
        reached = np.abs(points[costs - fit.cost < ALLOWED_CHI2 * residual_variance] - fit.servo).max(axis=0)
        cut_off = reached >= reach * SCAN_SPACING - 1e-9
        return np.maximum(reached, SCAN_SPACING / 2), cut_off, residual, residual_variance


def _lag_sigma(search: _Search, lag: int, residual_variance: float) -> float:
    """From the cost's profile over the neighbouring lag steps, each with its servo parameters refitted: the sigma of
    a parabola through them, but no less than the rounding to a step."""
    step = search.lags_ms[1] - search.lags_ms[0]
    here = search.by_lag[lag]
    rises = [(search.refine(n, here.servo).cost - here.cost) / residual_variance
             for n in (lag - 1, lag + 1) if 0 <= n < len(search.lags_ms)]
    if sum(rises) <= 0.0:
        return np.inf
    return max(step * np.sqrt(len(rises) / sum(rises)), step / np.sqrt(12.0))


def _estimate(p: Parameter, value: float, std: float, at_bound: bool, misfit: str) -> Estimate:
    unit = f" {p.unit}" if p.unit else ""
    if not np.isfinite(std) or 4.0 * std > (p.prior[1] - p.prior[0]) / 2:
        return _not_identifiable(f"sigma {std:.3g}{unit} at {value:.3g}{unit}: the data cannot pin it within half of "
                                 f"today's range ({p.prior[0]:g}-{p.prior[1]:g}{unit})", p.unit)
    notes = [f"at the search bound {p.bounds}, so the true value may lie beyond"] if at_bound else []
    return Estimate(float(value), float(std), p.unit, "; ".join(notes + ([misfit] if misfit else [])))


def dynamics_estimates(rec: Recording, imu: dict[str, Estimate]) -> dict[str, Estimate]:
    refuse = lambda reason: {p.name: _not_identifiable(reason, p.unit) for p in (*SERVO_PARAMETERS, LAG)}
    if rec.header is None or rec.header.variant != PICO_VARIANT:
        variant = "no header" if rec.header is None else f"variant {rec.header.variant or 'unset'}"
        return refuse(f"{variant}; the replay model is the Pico ({PICO_VARIANT})")
    seq = rec.ticks["seq"].astype(np.int64)
    lost = np.flatnonzero(np.diff(seq) != 1)
    ticks = int(lost[0]) + 1 if len(lost) else len(seq)
    settle = int(round(SETTLE_S / domain_rand.CONTROL_DT))
    if ticks <= settle or not still_ticks(rec)[:settle].all():
        return refuse("the recording does not begin with a robot standing still")
    replay = Replay(rec, ticks)
    if not replay.used.any():
        return refuse("no IMU sample with gyro and orientation after the settle")
    gyro_noise = imu["gyro_noise_std"].value or domain_rand.GYRO_NOISE_STD
    tilt_noise = np.radians(imu["rpy_noise_deg"].value or domain_rand.RPY_NOISE_DEG)
    search = _Search(replay, np.array([gyro_noise] * 3 + [tilt_noise] * 2))
    lag = search.run()
    sigma, cut_off, residual, residual_variance = _servo_sigma(search, lag)
    residual_rms = float(np.sqrt(np.mean(residual ** 2)))
    misfit = (f"the fit residual is {residual_rms:.1f} noise sigmas: the model misses the recording, so the sigma "
              f"understates the uncertainty") if residual_rms > MISFIT_RMS else ""
    servo = search.by_lag[lag].servo
    if cut_off.any():
        along = " and ".join(p.name for p, cut in zip(SERVO_PARAMETERS, cut_off) if cut)
        estimates = {p.name: _not_identifiable(f"the values the data allows run past where {along} would be "
                                               f"identifiable, and the two are fitted together", p.unit)
                     for p in SERVO_PARAMETERS}
    else:
        estimates = {p.name: _estimate(p, p.value(servo[i]), sigma[i] * (p.bounds[1] - p.bounds[0]),
                                       min(servo[i], 1.0 - servo[i]) < 0.01, misfit)
                     for i, p in enumerate(SERVO_PARAMETERS)}
    estimates[LAG.name] = _estimate(LAG, search.lags_ms[lag], _lag_sigma(search, lag, residual_variance),
                                    lag in (0, len(search.lags_ms) - 1), misfit)
    estimates["fit_residual_rms"] = Estimate(residual_rms, note="in noise sigmas; near 1 when the model explains the "
                                                                "recording down to the IMU noise")
    return estimates


def identify(rec: Recording) -> dict[str, Estimate]:
    imu = imu_estimates(rec)
    return {**imu, **latency_estimates(rec), **dynamics_estimates(rec, imu)}


def centre(estimates: dict[str, Estimate], source: str) -> dict:
    """The JSON domain_rand.Ranges.identified reads: the identified values, and why the rest are missing."""
    return {
        "source": source,
        "estimates": {name: {"value": e.value, "std": e.std, "unit": e.unit}
                      for name, e in estimates.items() if e.identified and name != "fit_residual_rms"},
        "not_identifiable": {name: e.note for name, e in estimates.items() if not e.identified},
    }
