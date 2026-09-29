import { ModesEnum, WalkGaits, type ControllerData } from '$lib/platform_shared/message'
import { kinConfig, type KinConfig, type Variant } from './kin-config'
import { BodyState, inverseKinematics } from './kinematics'
import { MotionState, RestState, StandState, WalkState, type CommandMsg } from './states'

// MotionService (esp32/src/motion.cpp): servo direction per joint, and the change below which an
// angle is not re-sent.
const DIR = [1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1]
const ANGLE_EPSILON = 0.1

/**
 * Port of the firmware's MotionService for one kinematics variant: mode, gait and controller input
 * in, servo-convention joint angles (degrees) out, one call per control tick.
 * tests/unit/firmware-motion.spec.ts pins it to the firmware headers.
 */
export class FirmwareMotion {
    readonly cfg: KinConfig
    body: BodyState
    private readonly rest: RestState
    private readonly stand: StandState
    private readonly walk: WalkState
    private state: MotionState | null = null
    private angles = new Array(12).fill(0)

    constructor(variant: Variant) {
        this.cfg = kinConfig(variant)
        this.body = new BodyState(this.cfg)
        this.rest = new RestState(this.cfg)
        this.stand = new StandState(this.cfg)
        this.walk = new WalkState(this.cfg)
    }

    setMode(mode: ModesEnum) {
        this.state?.end()
        this.state =
            mode === ModesEnum.REST ? this.rest
            : mode === ModesEnum.STAND ? this.stand
            : mode === ModesEnum.WALK ? this.walk
            : null
        this.state?.begin()
    }

    setGait(gait: WalkGaits) {
        if (gait === WalkGaits.TROT) this.walk.setModeTrot()
        else this.walk.setModeCrawl()
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
        this.state?.handleCommand(cmd)
    }

    /** One control tick. `imu` is [angleX, angleY] in radians, as Peripherals reports them. */
    update(dt: number, [angleX, angleY]: [number, number]): number[] {
        if (this.state) {
            this.state.updateImuOffsets(angleY, angleX)
            this.state.step(this.body, dt)
            inverseKinematics(this.cfg, this.body).forEach((angle, i) => {
                const directed = angle * DIR[i]
                if (Math.abs(directed - this.angles[i]) >= ANGLE_EPSILON) this.angles[i] = directed
            })
        }
        return [...this.angles]
    }
}
