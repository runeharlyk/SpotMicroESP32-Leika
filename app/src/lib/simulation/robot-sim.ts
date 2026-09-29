import type { MainModule, MjData, MjModel } from '@mujoco/mujoco'
import type { ControllerData, ModesEnum, WalkGaits } from '$lib/platform_shared/message'
import { CONTROL_DT } from './timing'

/** What a controller reads each tick: the app's controls, and the IMU as Peripherals reports it. */
export interface SimControls {
    input: ControllerData
    mode: ModesEnum
    gait: WalkGaits
    imu: [angleX: number, angleY: number]
}

export interface SimController {
    /** Starts over; returns the joint targets (radians, by joint name) the robot is spawned in. */
    reset(): Record<string, number>
    /** One 10 ms control tick; joint targets in radians, by joint name. */
    tick(controls: SimControls): Record<string, number>
}

export interface SimScene {
    /** Names the in-memory folder the scene is written to. */
    id: string
    sceneXml: string
    /** Mesh files by bare name, as the scenes' meshdir="meshes/" expects. */
    meshes: Record<string, Uint8Array>
}

/** The most simulated time one call may advance, so a tab returning from the background resumes instead of replaying. */
export const MAX_FRAME_SECONDS = 0.05
const FOOT_SITES = ['foot_fl', 'foot_fr', 'foot_rl', 'foot_rr']
const SITE_OBJECT = 6 // mjtObj.mjOBJ_SITE
const ACTUATOR_OBJECT = 19 // mjtObj.mjOBJ_ACTUATOR
const JOINT_OBJECT = 3 // mjtObj.mjOBJ_JOINT
const FALLEN_TILT_COS = Math.cos((60 * Math.PI) / 180)

/** One robot in MuJoCo, driven by a controller at 100 Hz with 5 physics steps per tick. */
export class RobotSim {
    private readonly model: MjModel
    private readonly data: MjData
    private readonly actuators: Map<string, number>
    private readonly jointQpos: Map<string, number>
    private readonly footSites: number[]
    private readonly substeps: number
    private spawnHeight = 0
    private controls: SimControls | undefined
    private pending = 0

    constructor(
        private readonly mujoco: MainModule,
        scene: SimScene,
        private readonly controller: SimController,
        private readonly footRadius: number
    ) {
        const scenePath = writeScene(mujoco, scene)
        this.model = mujoco.MjModel.from_xml_path(scenePath)
        this.data = new mujoco.MjData(this.model)
        this.substeps = Math.round(CONTROL_DT / this.model.opt.timestep)
        const name = (kind: number, id: number) => mujoco.mj_id2name(this.model, kind, id)
        this.actuators = new Map(
            Array.from({ length: this.model.nu }, (_, id) => [name(ACTUATOR_OBJECT, id), id])
        )
        this.jointQpos = new Map(
            Array.from({ length: this.model.njnt }, (_, id) => [
                name(JOINT_OBJECT, id),
                this.model.jnt_qposadr[id]
            ])
        )
        this.footSites = FOOT_SITES.map(site => mujoco.mj_name2id(this.model, SITE_OBJECT, site))
        this.reset()
    }

    /** Spawns the robot in its controller's starting pose, with the lowest foot resting on the floor. */
    reset() {
        this.mujoco.mj_resetData(this.model, this.data)
        const pose = this.controller.reset()
        this.apply(pose, true)
        this.data.qpos.set([0, 0, 0, 1, 0, 0, 0], 0)
        this.mujoco.mj_forward(this.model, this.data)
        const lowest = Math.min(...this.footSites.map(site => this.data.site_xpos[site * 3 + 2]))
        this.spawnHeight = this.footRadius - lowest
        this.data.qpos[2] = this.spawnHeight
        this.mujoco.mj_forward(this.model, this.data)
        this.pending = 0
    }

    setControls(controls: SimControls) {
        this.controls = controls
    }

    /** Advances by whole 10 ms control ticks, carrying the remainder to the next call. */
    step(seconds: number) {
        this.pending += Math.min(seconds, MAX_FRAME_SECONDS)
        while (this.pending >= CONTROL_DT - 1e-12) {
            if (this.controls) this.apply(this.controller.tick(this.controls), false)
            for (let i = 0; i < this.substeps; i++) this.mujoco.mj_step(this.model, this.data)
            this.pending -= CONTROL_DT
        }
    }

    time(): number {
        return this.data.time
    }

    basePosition(): [number, number, number] {
        return [this.data.qpos[0], this.data.qpos[1], this.data.qpos[2]]
    }

    baseQuaternion(): [w: number, x: number, y: number, z: number] {
        return [this.data.qpos[3], this.data.qpos[4], this.data.qpos[5], this.data.qpos[6]]
    }

    jointAngles(): Record<string, number> {
        return Object.fromEntries(
            [...this.actuators.keys()].map(joint => [
                joint,
                this.data.qpos[this.jointQpos.get(joint)!]
            ])
        )
    }

    /** Below half its spawn height, or tilted beyond 60 degrees. */
    hasFallen(): boolean {
        const [, x, y] = this.baseQuaternion()
        const upZ = 1 - 2 * (x * x + y * y)
        return this.data.qpos[2] < this.spawnHeight / 2 || upZ < FALLEN_TILT_COS
    }

    dispose() {
        this.data.delete()
        this.model.delete()
    }

    private apply(targets: Record<string, number>, setPosition: boolean) {
        for (const [joint, angle] of Object.entries(targets)) {
            const actuator = this.actuators.get(joint)
            if (actuator === undefined)
                throw new Error(`The scene has no actuator for joint ${joint}`)
            this.data.ctrl[actuator] = angle
            if (setPosition) this.data.qpos[this.jointQpos.get(joint)!] = angle
        }
    }
}

function writeScene(mujoco: MainModule, { id, sceneXml, meshes }: SimScene) {
    const fs = mujoco.FS
    const dir = `/scenes/${id}`
    for (const folder of ['/scenes', dir, `${dir}/meshes`]) {
        if (!fs.analyzePath(folder, false).exists) fs.mkdir(folder)
    }
    fs.writeFile(`${dir}/scene.xml`, sceneXml)
    for (const [name, bytes] of Object.entries(meshes)) fs.writeFile(`${dir}/meshes/${name}`, bytes)
    return `${dir}/scene.xml`
}
