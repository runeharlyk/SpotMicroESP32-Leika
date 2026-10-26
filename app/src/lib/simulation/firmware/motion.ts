import {
    AnimationState,
    AnimationStatus,
    ModesEnum,
    WalkGaits,
    type AnimationParam,
    type ControllerData
} from '$lib/platform_shared/message'
import type { Animation } from '$lib/platform_shared/animation'
import {
    Player,
    PlayerState,
    addFootOffset,
    capturePose,
    poseToAngles,
    toBodyState
} from '$lib/animation/player'
import { kinConfig, type KinConfig, type Variant } from './kin-config'
import { BodyState, inverseKinematics } from './kinematics'
import { MotionState, RestState, StandState, WalkState, type CommandMsg } from './states'

// MotionService (esp32/src/motion.cpp): servo direction per joint, and the change below which an
// angle is not re-sent.
export const DIR = [1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1]
export const ANGLE_EPSILON = 0.1

/** AnimateState (motion_states/animate_state.h) playing a clip, with the player the editor's preview uses. */
class Animate {
    player: Player
    pending: { clip: Animation; params: AnimationParam[] } | undefined
    name = ''
    clampedMask = 0

    constructor(private readonly cfg: KinConfig) {
        this.player = new Player(cfg)
    }

    stop() {
        this.pending = undefined
        this.player.stop()
    }

    abandon() {
        this.pending = undefined
        this.player = new Player(this.cfg)
    }

    finished() {
        return !this.pending && this.player.state === PlayerState.IDLE
    }

    /** The clip's angles (deg, IK order) for one tick; the body follows, so the next state starts where it left. */
    step(body: BodyState, dt: number): number[] {
        if (this.pending) {
            const { clip, params } = this.pending
            this.player.play(clip, params, capturePose(this.cfg, body, body.ym), body.ym)
            this.name = clip.name
            this.pending = undefined
        }
        const pose = this.player.update(dt)
        const base = this.player.base()
        Object.assign(body, toBodyState(this.cfg, pose.body, base))
        pose.legs.forEach((leg, i) => {
            if (!leg.joints) addFootOffset(body, i, leg.v)
        })
        const { angles, mask } = poseToAngles(this.cfg, pose, base)
        this.clampedMask = mask
        return angles
    }
}

/**
 * Port of the firmware's MotionService for one kinematics variant: mode, gait, controller input and clips
 * in, servo-convention joint angles (degrees) out, one call per control tick.
 * tests/unit/firmware-motion.spec.ts pins it to the firmware headers.
 */
export class FirmwareMotion {
    readonly cfg: KinConfig
    body: BodyState
    private readonly rest: RestState
    private readonly stand: StandState
    private readonly walk: WalkState
    private readonly animate: Animate
    private state: MotionState | Animate | null = null
    private currentMode = ModesEnum.DEACTIVATED
    private returnMode = ModesEnum.DEACTIVATED
    private angles = new Array(12).fill(0)

    constructor(variant: Variant) {
        this.cfg = kinConfig(variant)
        this.body = new BodyState(this.cfg)
        this.rest = new RestState(this.cfg)
        this.stand = new StandState(this.cfg)
        this.walk = new WalkState(this.cfg)
        this.animate = new Animate(this.cfg)
    }

    /** The mode the robot is in: ANIMATE while a clip plays, which no request selects. */
    get mode() {
        return this.currentMode
    }

    /** A clip is left through its exit back to stance, then the mode asked for applies; deactivating is never delayed. */
    setMode(mode: ModesEnum) {
        if (mode === ModesEnum.ANIMATE) return
        if (this.state === this.animate && mode !== ModesEnum.DEACTIVATED) {
            this.returnMode = mode
            this.animate.stop()
            return
        }
        this.applyMode(mode)
    }

    /** Plays `clip` from the live legs on the next tick; false while the legs are unpowered, as the robot refuses it. */
    playClip(clip: Animation, params: AnimationParam[]): boolean {
        if (!this.state) return false
        if (this.state !== this.animate) {
            // A clip ends in stance: the walk resumes from STAND.
            this.returnMode =
                this.currentMode === ModesEnum.WALK ? ModesEnum.STAND : this.currentMode
            this.switchTo(this.animate)
            this.currentMode = ModesEnum.ANIMATE
        }
        this.animate.pending = { clip, params }
        return true
    }

    stopClip() {
        if (this.state === this.animate) this.animate.stop()
    }

    /** The status the robot broadcasts; PlayerState counts its states as AnimationState does. */
    animationStatus(): AnimationStatus {
        const { name, player, clampedMask } = this.animate
        const state = player.state as number as AnimationState
        return AnimationStatus.create({ name, state, t: player.t, clampedMask })
    }

    setGait(gait: WalkGaits) {
        this.walk.setAutoGait(gait === WalkGaits.AUTO)
        if (gait === WalkGaits.TROT) this.walk.selectGait(false)
        else if (gait === WalkGaits.CRAWL) this.walk.selectGait(true)
    }

    /** MotionService::useServoSpeedLimit: how fast the servos turn a joint (deg/s), which caps the walk. */
    useServoSpeedLimit(limit: number) {
        this.walk.useServoSpeedLimit(limit)
    }

    /** The schedule the legs walk, TROT or CRAWL, also when AUTO picked it. */
    walkedGait() {
        return this.walk.crawling() ? WalkGaits.CRAWL : WalkGaits.TROT
    }

    handleInput(data: ControllerData) {
        const cmd: CommandMsg = {
            lx: data.left?.x ?? 0,
            ly: data.left?.y ?? 0,
            rx: data.right?.x ?? 0,
            ry: data.right?.y ?? 0,
            h: data.height,
            s: data.speed,
            s1: data.s1
        }
        if (this.state instanceof MotionState) this.state.handleCommand(cmd)
    }

    /**
     * One control tick. `imu` is [roll, pitch] in radians, REP-103 (positive roll lifts the left side, positive
     * pitch lowers the nose). The kinematics' omega and psi turn the other way from both, as in state.h.
     */
    update(dt: number, [roll, pitch]: [number, number]): number[] {
        if (this.state === this.animate) {
            const angles = this.animate.step(this.body, dt)
            if (this.animate.finished()) this.applyMode(this.returnMode)
            this.send(angles)
        } else if (this.state instanceof MotionState) {
            this.state.updateImuOffsets(-roll, -pitch)
            this.state.step(this.body, dt)
            this.send(inverseKinematics(this.cfg, this.body))
        }
        return [...this.angles]
    }

    private applyMode(mode: ModesEnum) {
        if (this.state === this.animate) this.animate.abandon()
        this.currentMode = mode
        this.switchTo(
            mode === ModesEnum.REST ? this.rest
            : mode === ModesEnum.STAND ? this.stand
            : mode === ModesEnum.WALK ? this.walk
            : null
        )
    }

    private switchTo(next: MotionState | Animate | null) {
        if (this.state instanceof MotionState) this.state.end()
        this.state = next
        if (next instanceof MotionState) {
            next.resetSmoothing()
            next.begin()
        }
    }

    private send(angles: number[]) {
        angles.forEach((angle, i) => {
            const directed = angle * DIR[i]
            if (Math.abs(directed - this.angles[i]) >= ANGLE_EPSILON) this.angles[i] = directed
        })
    }
}
