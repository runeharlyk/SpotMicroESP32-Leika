import { describe, it, expect, afterEach } from 'vitest'
import { get } from 'svelte/store'
import { WebSocketServer } from 'ws'
import { Message } from '../../src/lib/platform_shared/message'
import { socket } from '../../src/lib/stores/socket'
import { useFeatureFlags } from '../../src/lib/stores/featureFlags'
import { apiLocation, robotSocketUrl } from '../../src/lib/stores/location-store'
import { robots } from '../../src/lib/stores/robots'
import { renameConnectedRobot } from '../../src/lib/services/robot-names'

type FakeRobot = { deviceId: string; robotName: string; variant: string }

// A server that answers the features and rename requests the way the firmware does.
async function startFakeRobot(port: number, robot: FakeRobot) {
    const wss = new WebSocketServer({ port })
    await new Promise<void>(resolve => wss.on('listening', () => resolve()))
    wss.on('connection', client =>
        client.on('message', raw => {
            const message = Message.decode(new Uint8Array(raw as Buffer))
            const request = message.correlationRequest
            if (!request) return
            let statusCode = 200
            if (request.robotNameUpdate) {
                // The firmware holds 32 bytes of name: a longer one fails to decode, and goes unanswered.
                if (new TextEncoder().encode(request.robotNameUpdate.name).length > 32) return
                const name = request.robotNameUpdate.name.trim()
                if (name) robot.robotName = name
                else statusCode = 400
            } else if (!request.featuresDataRequest) return
            const reply = Message.create({
                correlationResponse: {
                    correlationId: request.correlationId,
                    statusCode,
                    featuresDataResponse: { ...robot, camera: true }
                }
            })
            client.send(Message.encode(reply).finish())
        })
    )
    return wss
}

const until = async (condition: () => boolean) => {
    for (let waited = 0; waited < 3000 && !condition(); waited += 20) {
        await new Promise(resolve => setTimeout(resolve, 20))
    }
    expect(condition()).toBe(true)
}

describe('robot identification on connect', () => {
    const servers: WebSocketServer[] = []

    afterEach(async () => {
        for (const wss of servers.splice(0)) {
            wss.clients.forEach(client => client.close())
            await new Promise<void>(resolve => wss.close(() => resolve()))
        }
    })

    it('identifies each robot the socket connects to and refreshes the feature flags', async () => {
        const pico = {
            deviceId: '240AC40BA1F3',
            robotName: 'Pico one',
            variant: 'SPOTMICRO_ESP32_MINI'
        }
        const yertle = {
            deviceId: 'A4CF12000001',
            robotName: 'Yertle',
            variant: 'SPOTMICRO_YERTLE'
        }
        servers.push(await startFakeRobot(8901, pico), await startFakeRobot(8902, yertle))
        const features = useFeatureFlags()

        apiLocation.set('localhost:8901')
        socket.init(robotSocketUrl())
        await until(() => get(features).deviceId === pico.deviceId)

        apiLocation.set('localhost:8902')
        socket.init(robotSocketUrl())
        await until(() => get(features).deviceId === yertle.deviceId)

        expect(get(features).variant).toBe('SPOTMICRO_YERTLE')
        expect(get(robots).map(robot => [robot.id, robot.name, robot.addresses])).toEqual([
            [pico.deviceId, 'Pico one', ['localhost:8901']],
            [yertle.deviceId, 'Yertle', ['localhost:8902']]
        ])
    })

    it('renames the connected robot on the robot and in the list', async () => {
        const pico = {
            deviceId: '240AC40BA1F3',
            robotName: 'Pico one',
            variant: 'SPOTMICRO_ESP32_MINI'
        }
        servers.push(await startFakeRobot(8903, pico))
        const features = useFeatureFlags()
        apiLocation.set('localhost:8903')
        socket.init(robotSocketUrl())
        await until(() => get(features).deviceId === pico.deviceId)

        expect(await renameConnectedRobot('  Pico two ')).toBeNull()
        expect(pico.robotName).toBe('Pico two')
        expect(get(robots).find(robot => robot.id === pico.deviceId)?.name).toBe('Pico two')

        expect(await renameConnectedRobot('   ')).toMatch(/rejected/i)
        expect(get(robots).find(robot => robot.id === pico.deviceId)?.name).toBe('Pico two')

        // Too long for the robot to read: refused at once, not after the request times out.
        expect(await renameConnectedRobot('a'.repeat(33))).toMatch(/32/)
        expect(await renameConnectedRobot('ø'.repeat(17))).toMatch(/32/)
        expect(await renameConnectedRobot(` ${'ø'.repeat(16)} `)).toBeNull()
        expect(pico.robotName).toBe('ø'.repeat(16))
    })

    it('closes the previous robot connection when switching robots', async () => {
        const first = await startFakeRobot(8904, {
            deviceId: 'AAAAAA000001',
            robotName: 'A',
            variant: ''
        })
        const second = await startFakeRobot(8905, {
            deviceId: 'AAAAAA000002',
            robotName: 'B',
            variant: ''
        })
        servers.push(first, second)
        const features = useFeatureFlags()

        socket.init(robotSocketUrl('localhost:8904'))
        await until(() => first.clients.size === 1)
        apiLocation.set('localhost:8905')
        socket.init(robotSocketUrl())
        await until(() => get(features).deviceId === 'AAAAAA000002')

        await until(() => first.clients.size === 0)
        expect(second.clients.size).toBe(1)
    })
})
