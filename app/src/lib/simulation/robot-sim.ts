import type { MainModule, MjData, MjModel } from '@mujoco/mujoco'
import type { ControllerData, ModesEnum, WalkGaits } from '$lib/platform_shared/message'
import { CONTROL_DT } from './timing'

/** What a controller reads each tick: the app's controls, and the IMU as Peripherals reports it. */
export interface SimControls {
    input: ControllerData
    mode: ModesEnum
    gait: WalkGaits
    imu: [roll: number, pitch: number]
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
const ACTUATOR_OBJECT = 19 // mjtObj.mjOBJ_ACTUATOR
const JOINT_OBJECT = 3 // mjtObj.mjOBJ_JOINT
const FALLEN_TILT_COS = Math.cos((60 * Math.PI) / 180)
// mjtGeom
const SPHERE = 2
const BOX = 6
const MESH = 7

/** One robot in MuJoCo, driven by a controller at 100 Hz with 5 physics steps per tick. */
export class RobotSim {
    private readonly model: MjModel
    private readonly data: MjData
    private readonly actuators: Map<string, number>
    private readonly jointQpos: Map<string, number>
    private readonly substeps: number
    private spawnHeight = 0
    private controls: SimControls | undefined
    private pending = 0

    constructor(
        private readonly mujoco: MainModule,
        scene: SimScene,
        private readonly controller: SimController
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
        this.reset()
    }

    /** Spawns the robot in its controller's starting pose, its lowest collision point on the floor. */
    reset() {
        this.mujoco.mj_resetData(this.model, this.data)
        const pose = this.controller.reset()
        this.apply(pose, true)
        this.data.qpos.set([0, 0, 0, 1, 0, 0, 0], 0)
        this.mujoco.mj_forward(this.model, this.data)
        this.spawnHeight = -this.lowestPoint()
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

    /** The height of the lowest point of the robot's collision geometry. */
    private lowestPoint(): number {
        const { model, data } = this
        let lowest = Infinity
        for (let geom = 0; geom < model.ngeom; geom++) {
            const collides = model.geom_contype[geom] || model.geom_conaffinity[geom]
            if (model.geom_bodyid[geom] === 0 || !collides) continue
            const z = data.geom_xpos[geom * 3 + 2]
            // Row z of the geom's rotation, which maps a point in the geom's frame to world height.
            const [rx, ry, rz] = data.geom_xmat.subarray(geom * 9 + 6, geom * 9 + 9)
            const size = model.geom_size.subarray(geom * 3, geom * 3 + 3)
            const type = model.geom_type[geom]
            if (type === SPHERE) lowest = Math.min(lowest, z - size[0])
            else if (type === BOX)
                lowest = Math.min(
                    lowest,
                    z - Math.abs(rx) * size[0] - Math.abs(ry) * size[1] - Math.abs(rz) * size[2]
                )
            else if (type === MESH) {
                const mesh = model.geom_dataid[geom]
                const start = model.mesh_vertadr[mesh] * 3
                const end = start + model.mesh_vertnum[mesh] * 3
                const vertex = model.mesh_vert
                for (let v = start; v < end; v += 3) {
                    lowest = Math.min(
                        lowest,
                        z + rx * vertex[v] + ry * vertex[v + 1] + rz * vertex[v + 2]
                    )
                }
            } else throw new Error(`Spawning on the floor does not handle geom type ${type}`)
        }
        return lowest
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
