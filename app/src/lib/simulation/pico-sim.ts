import type { MainModule, MjData, MjModel } from '@mujoco/mujoco'
import {
    CONTROL_DT,
    JOINT_NAMES,
    PicoController,
    STAND_Z,
    type Command,
    type GaitCoef,
    type Vec3
} from './pico-gait'

export interface SimAssets {
    sceneXml: string
    /** Mesh files by bare name, e.g. base_link.stl, as scene.xml's meshdir="meshes/" expects. */
    meshes: Record<string, Uint8Array>
    gaitCoef: GaitCoef
}

/** The most simulated time one call may advance, so a tab returning from the background resumes instead of replaying. */
export const MAX_FRAME_SECONDS = 0.05
const SCENE_DIR = '/spot_pico'
const JOINT_OBJECT = 3 // mjtObj.mjOBJ_JOINT
const ACTUATOR_OBJECT = 19 // mjtObj.mjOBJ_ACTUATOR
const FALLEN_TILT_COS = Math.cos((60 * Math.PI) / 180)

export class PicoSim {
    private readonly model: MjModel
    private readonly data: MjData
    private readonly coef: GaitCoef
    private controller: PicoController
    private readonly actuators: number[]
    private readonly jointQpos: number[]
    private readonly substeps: number
    private command: Command = [0, 0, 0]
    private pending = 0

    constructor(
        private readonly mujoco: MainModule,
        assets: SimAssets
    ) {
        writeScene(mujoco, assets)
        this.model = mujoco.MjModel.from_xml_path(`${SCENE_DIR}/scene.xml`)
        this.data = new mujoco.MjData(this.model)
        this.coef = assets.gaitCoef
        this.controller = new PicoController(this.coef)
        this.substeps = Math.round(CONTROL_DT / this.model.opt.timestep)
        this.actuators = JOINT_NAMES.map(name =>
            mujoco.mj_name2id(this.model, ACTUATOR_OBJECT, name)
        )
        this.jointQpos = JOINT_NAMES.map(
            name => this.model.jnt_qposadr[mujoco.mj_name2id(this.model, JOINT_OBJECT, name)]
        )
        this.reset()
    }

    /** Back to the stand pose at the origin, like SpotPicoSim.reset_to_stand, with a fresh gait phase. */
    reset() {
        this.mujoco.mj_resetData(this.model, this.data)
        this.controller = new PicoController(this.coef)
        const stand = new PicoController(this.coef).tick([0, 0, 0])
        this.data.qpos.set([0, 0, STAND_Z, 1, 0, 0, 0], 0)
        this.jointQpos.forEach((address, i) => (this.data.qpos[address] = stand[i]))
        this.actuators.forEach((actuator, i) => (this.data.ctrl[actuator] = stand[i]))
        this.mujoco.mj_forward(this.model, this.data)
        this.pending = 0
    }

    setCommand(cmd: Command) {
        this.command = cmd
    }

    /** Advances by whole 10 ms control ticks, carrying the remainder to the next call. */
    step(seconds: number) {
        this.pending += Math.min(seconds, MAX_FRAME_SECONDS)
        while (this.pending >= CONTROL_DT - 1e-12) {
            const targets = this.controller.tick(this.command)
            this.actuators.forEach((actuator, i) => (this.data.ctrl[actuator] = targets[i]))
            for (let i = 0; i < this.substeps; i++) this.mujoco.mj_step(this.model, this.data)
            this.pending -= CONTROL_DT
        }
    }

    time(): number {
        return this.data.time
    }

    basePosition(): Vec3 {
        return [this.data.qpos[0], this.data.qpos[1], this.data.qpos[2]]
    }

    baseQuaternion(): [number, number, number, number] {
        return [this.data.qpos[3], this.data.qpos[4], this.data.qpos[5], this.data.qpos[6]]
    }

    jointAngles(): Record<string, number> {
        return Object.fromEntries(
            JOINT_NAMES.map((name, i) => [name, this.data.qpos[this.jointQpos[i]]])
        )
    }

    /** Below half the stand height, or tilted beyond 60 degrees. */
    hasFallen(): boolean {
        const [w, x, y] = this.baseQuaternion()
        const upZ = 1 - 2 * (x * x + y * y)
        return this.data.qpos[2] < STAND_Z / 2 || upZ < FALLEN_TILT_COS || w === 0
    }

    dispose() {
        this.data.delete()
        this.model.delete()
    }
}

function writeScene(mujoco: MainModule, { sceneXml, meshes }: SimAssets) {
    const fs = mujoco.FS
    for (const dir of [SCENE_DIR, `${SCENE_DIR}/meshes`]) {
        if (!fs.analyzePath(dir, false).exists) fs.mkdir(dir)
    }
    fs.writeFile(`${SCENE_DIR}/scene.xml`, sceneXml)
    for (const [name, bytes] of Object.entries(meshes))
        fs.writeFile(`${SCENE_DIR}/meshes/${name}`, bytes)
}
