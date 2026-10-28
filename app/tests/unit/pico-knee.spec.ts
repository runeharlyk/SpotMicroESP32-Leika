import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { Quaternion, Vector3, type Object3D } from 'three'
import URDFLoader, { type URDFRobot } from 'urdf-loader'
import { picoUrdfForApp } from '../../scripts/pico-model.js'
import { crankFromTibia, tibiaFromCrank } from '../../src/lib/pico-knee'
import { calibrationJointValues, stanceJointValues } from '../../src/lib/robot-pose'
import { jointTargets } from '../../src/lib/simulation/controllers'
import { JOINT_MAPS } from '../../src/lib/simulation/robots'
import { kinConfig } from '../../src/lib/simulation/firmware/kin-config'
import { DIR } from '../../src/lib/simulation/firmware/motion'
import { referencePose } from '../../src/lib/calibration/leg-pose'
import type { JointModel } from '../../src/lib/platform_shared/api'

const rad = (degrees: number) => (degrees * Math.PI) / 180
const deg = (radians: number) => (radians * 180) / Math.PI

// joint_model.h's models, with the Pico's calibrated centres (2026-10-01) and Spot Micro's default.
const PICO_MODEL: JointModel = {
    direction: [1, 1, -1, 1, -1, 1, -1, 1, -1, -1, -1, 1],
    centerAngle: [0, -45, -90, 0, 45, 90, 0, -45, -90, 0, 45, 90],
    pwmPerDegree: 2
}
const PICO_CENTERS = [295, 243, 291, 268, 305, 270, 258, 260, 287, 289, 301, 273]
const SPOT_MODEL: JointModel = {
    direction: [-1, 1, 1, -1, -1, -1, 1, 1, 1, 1, -1, -1],
    centerAngle: [0, -45, 90, 0, 45, -90, 0, -45, 90, 0, 45, -90],
    pwmPerDegree: 2
}

const pico = path.resolve(__dirname, '../../../simulation/src/resources/spot_pico')
function picoModel(): URDFRobot {
    const read = (file: string) => readFileSync(path.join(pico, file), 'utf8')
    const loader = new URDFLoader()
    loader.loadMeshCb = (_path, _manager, done) => done(null as unknown as Object3D)
    return loader.parse(picoUrdfForApp(read('spot_pico.urdf'), read('scene.xml')))
}

describe("the Pico's knee four-bar", () => {
    // linkage.py's own numbers: tibia_from_servo and servo_from_tibia with the femur at its stance.
    it.each([
        [-30, -46.7389],
        [-10, -15.8261],
        [5, 8.4972],
        [15, 27.1726],
        [25, 53.4189]
    ])('turns the tibia as linkage.py does for a crank at %d degrees', (crank, tibia) =>
        expect(deg(tibiaFromCrank(rad(crank)))).toBeCloseTo(tibia, 3)
    )

    it.each([
        [-60, -37.9242],
        [-30, -19.2166],
        [0, -0.054],
        [30, 16.342],
        [55, 25.3938]
    ])('needs the crank linkage.py needs for a tibia at %d degrees', (tibia, crank) =>
        expect(deg(crankFromTibia(rad(tibia)))).toBeCloseTo(crank, 3)
    )
})

describe('the model posed by the calibration page', () => {
    it("puts Spot Micro's selected joint at the angle its PWM means and holds the others in the stance", () => {
        const variant = 'SPOTMICRO_ESP32'
        const centers = new Array(12).fill(306)
        const stance = stanceJointValues(variant)
        // Front-right femur in the firmware's order, 30 counts up: servo angle direction * (15 - centre angle).
        const joint = 4
        const joints = calibrationJointValues(variant, SPOT_MODEL, centers, joint, 336)
        const names = JOINT_MAPS[variant].joints
        names.forEach((name, i) => {
            if (i === joint) expect(deg(joints[name])).toBeCloseTo(-1 * (15 - 45), 3)
            else expect(joints[name]).toBeCloseTo(stance[name], 6)
        })
    })

    const variant = 'SPOTMICRO_ESP32_MINI'
    const { joints: names, sign } = JOINT_MAPS[variant]
    const cfg = kinConfig(variant)
    const linear = (legIk: number[]) =>
        jointTargets(
            JOINT_MAPS[variant],
            legIk.map((angle, i) => angle * DIR[i])
        )

    it("puts the Pico's knees where the joint map does with every joint at its centre PWM", () => {
        const centers = new Array(12).fill(300)
        const joints = calibrationJointValues(variant, PICO_MODEL, centers, -1, 300)
        const expected = linear(referencePose(cfg, PICO_MODEL, -1))
        for (const name of names) expect(joints[name]).toBeCloseTo(expected[name], 9)
    })

    // Near the reference the tibia turns about 1.7 times the horn (linkage.py's ratio); the joint map turns it 1:1.
    it.each([0, 1, 2, 3])(
        "turns leg %d's tibia the joint map's way, but further, as its knee PWM rises",
        leg => {
            const knee = leg * 3 + 2
            const centers = new Array(12).fill(300)
            const tibia = (pwm: number) =>
                calibrationJointValues(variant, PICO_MODEL, centers, knee, pwm)[names[knee]]
            const step = 2
            const turned = tibia(300 + step) - tibia(300)
            // leg-pose.ts servoAngleFromPwm: the servo angle turns direction / pwmPerDegree per count.
            const mapTurned =
                sign[knee] * rad((PICO_MODEL.direction[knee] * step) / PICO_MODEL.pwmPerDegree)
            expect(turned / mapTurned).toBeGreaterThan(1.6)
            expect(turned / mapTurned).toBeLessThan(1.9)
        }
    )

    // The knee servo sits in the coxa, so a femur turning under a still horn turns the crank against the femur, and
    // the tibia, 1.7 times the crank's turn the other way, ends up turned against the femur in the world. A tibia axis
    // taken the wrong way round would turn it with the femur instead.
    it.each([0, 1, 2, 3])(
        "turns leg %d's tibia against its femur when only the femur's PWM moves",
        leg => {
            const robot = picoModel()
            const [, femurName, kneeName] = names.slice(leg * 3, leg * 3 + 3)
            const femur = robot.joints[femurName]
            const tibiaLink = robot.joints[kneeName].children.find(
                child => (child as { isURDFLink?: boolean }).isURDFLink
            )!
            const femurLink = femur.children.find(
                child => (child as { isURDFLink?: boolean }).isURDFLink
            )!
            const pose = (pwm: number) => {
                const joints = calibrationJointValues(
                    variant,
                    PICO_MODEL,
                    PICO_CENTERS,
                    leg * 3 + 1,
                    pwm
                )
                for (const [name, value] of Object.entries(joints))
                    robot.joints[name].setJointValue(value)
                robot.updateMatrixWorld(true)
                return {
                    femur: femurLink.getWorldQuaternion(new Quaternion()),
                    tibia: tibiaLink.getWorldQuaternion(new Quaternion())
                }
            }
            const center = PICO_CENTERS[leg * 3 + 1]
            const before = pose(center)
            const after = pose(center + 10)
            const axis = new Vector3(1, 0, 0)
                .applyQuaternion(femur.parent!.getWorldQuaternion(new Quaternion()))
                .normalize()
            const turn = (from: Quaternion, to: Quaternion) => {
                const q = to.clone().multiply(from.clone().invert())
                return 2 * Math.atan2(new Vector3(q.x, q.y, q.z).dot(axis), q.w)
            }
            const ratio = turn(before.tibia, after.tibia) / turn(before.femur, after.femur)
            expect(ratio).toBeLessThan(-0.5)
            expect(ratio).toBeGreaterThan(-0.95)
        }
    )
})
