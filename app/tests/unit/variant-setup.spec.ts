import { describe, it, expect, afterEach } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { get } from 'svelte/store'
import { WebSocketServer } from 'ws'
import VariantSetup from '../../src/lib/components/VariantSetup.svelte'
import { KinematicsVariant, Message } from '../../src/lib/platform_shared/message'
import { socket } from '../../src/lib/stores/socket'
import {
    needsVariantChoice,
    useFeatureFlags,
    variantChoiceNeeded
} from '../../src/lib/stores/featureFlags'
import { robotSocketUrl } from '../../src/lib/stores/location-store'

type FakeRobot = {
    deviceId: string
    variant: string
    answerFeatures: boolean
    /** A mode moves the legs, so the robot refuses to switch. */
    moving?: boolean
}

const VARIANT_NAMES: Record<number, string> = {
    [KinematicsVariant.SPOTMICRO_ESP32]: 'SPOTMICRO_ESP32',
    [KinematicsVariant.SPOTMICRO_ESP32_MINI]: 'SPOTMICRO_ESP32_MINI',
    [KinematicsVariant.SPOTMICRO_YERTLE]: 'SPOTMICRO_YERTLE'
}

// Answers as the firmware does: a variant is switched to and the reply reports it, unless the legs move.
async function startFakeRobot(port: number, robot: FakeRobot) {
    const chosen: KinematicsVariant[] = []
    const wss = new WebSocketServer({ port })
    await new Promise<void>(resolve => wss.on('listening', () => resolve()))
    wss.on('connection', client =>
        client.on('message', raw => {
            const request = Message.decode(new Uint8Array(raw as Buffer)).correlationRequest
            if (!request) return
            if (request.featuresDataRequest && !robot.answerFeatures) return
            const update = request.robotVariantUpdate
            if (!request.featuresDataRequest && !update) return
            if (update) chosen.push(update.variant)
            const refused = update !== undefined && robot.moving === true
            if (update && !refused) robot.variant = VARIANT_NAMES[update.variant]
            const reply = Message.create({
                correlationResponse: {
                    correlationId: request.correlationId,
                    statusCode: refused ? 409 : 200,
                    errorMessage: refused ? 'Deactivate first' : '',
                    featuresDataResponse: { deviceId: robot.deviceId, variant: robot.variant }
                }
            })
            client.send(Message.encode(reply).finish())
        })
    )
    return { wss, chosen }
}

const pick = (label: RegExp) =>
    [...document.body.querySelectorAll<HTMLButtonElement>('[role="dialog"] button')]
        .find(button => label.test(button.textContent!))!
        .click()

const until = async (condition: () => boolean, timeout = 5000) => {
    for (let waited = 0; waited < timeout && !condition(); waited += 20) {
        await new Promise(resolve => setTimeout(resolve, 20))
    }
    expect(condition()).toBe(true)
}

describe('whether the robot needs to be told its variant', () => {
    it('asks only a connected robot that reported no variant', () => {
        expect(needsVariantChoice(true, { variant: '' })).toBe(true)
        expect(needsVariantChoice(true, { variant: 'SPOTMICRO_YERTLE' })).toBe(false)
        expect(needsVariantChoice(true, null)).toBe(false)
        expect(needsVariantChoice(false, { variant: '' })).toBe(false)
    })
})

describe('variant setup step', () => {
    const servers: WebSocketServer[] = []
    let component: ReturnType<typeof mount> | undefined

    afterEach(async () => {
        if (component) unmount(component)
        document.body.innerHTML = ''
        for (const wss of servers.splice(0)) {
            wss.clients.forEach(client => client.terminate())
            await new Promise<void>(resolve => wss.close(() => resolve()))
        }
    })

    const dialog = () => {
        flushSync()
        return document.body.querySelector('[role="dialog"]')
    }

    it('waits for the connected robot instead of trusting the flags kept from the last one', async () => {
        useFeatureFlags().set({ variant: '' })
        const robot = {
            deviceId: 'AAAAAA000011',
            variant: 'SPOTMICRO_YERTLE',
            answerFeatures: false
        }
        const { wss } = await startFakeRobot(8911, robot)
        servers.push(wss)
        component = mount(VariantSetup, { target: document.body })

        socket.init(robotSocketUrl('localhost:8911'))
        await until(() => get(socket))
        await new Promise(resolve => setTimeout(resolve, 100))

        expect(get(variantChoiceNeeded)).toBe(false)
        expect(dialog()).toBeNull()
    })

    it('sends the chosen variant and closes on the reply, with the connection kept', async () => {
        const robot = { deviceId: 'AAAAAA000012', variant: '', answerFeatures: true }
        const { wss, chosen } = await startFakeRobot(8912, robot)
        servers.push(wss)
        component = mount(VariantSetup, { target: document.body })

        socket.init(robotSocketUrl('localhost:8912'))
        await until(() => dialog() !== null)
        expect(dialog()!.textContent).toMatch(/Which robot is this\?/)

        pick(/Mini/)

        await until(() => dialog() === null)
        expect(chosen).toEqual([KinematicsVariant.SPOTMICRO_ESP32_MINI])
        expect(get(useFeatureFlags()).variant).toBe('SPOTMICRO_ESP32_MINI')
        expect(get(socket)).toBe(true)
    })

    it('keeps asking when the robot refuses to switch', async () => {
        const robot = { deviceId: 'AAAAAA000014', variant: '', answerFeatures: true, moving: true }
        const { wss, chosen } = await startFakeRobot(8914, robot)
        servers.push(wss)
        component = mount(VariantSetup, { target: document.body })

        socket.init(robotSocketUrl('localhost:8914'))
        await until(() => dialog() !== null)
        pick(/Yertle/)
        await until(() => chosen.length === 1)
        await until(() => !dialog()!.querySelector('button[disabled]'))

        expect(dialog()!.textContent).toMatch(/Which robot is this\?/)
        expect(dialog()!.querySelectorAll('button')).toHaveLength(3)
    })

    it('stays hidden for a robot that knows its variant', async () => {
        const robot = { deviceId: 'AAAAAA000013', variant: 'SPOTMICRO_ESP32', answerFeatures: true }
        const { wss } = await startFakeRobot(8913, robot)
        servers.push(wss)
        component = mount(VariantSetup, { target: document.body })

        socket.init(robotSocketUrl('localhost:8913'))
        await until(() => get(useFeatureFlags()).deviceId === robot.deviceId)

        expect(dialog()).toBeNull()
    })
})
