import { describe, it, expect, beforeEach, vi } from 'vitest'
import { get } from 'svelte/store'

// The store reads localStorage when its module loads, so each test imports a fresh copy.
async function loadRobots(saved?: unknown) {
    localStorage.clear()
    if (saved !== undefined) localStorage.setItem('robots', JSON.stringify(saved))
    vi.resetModules()
    return import('../../src/lib/stores/robots')
}

const pico = { deviceId: '240AC40BA1F3', name: 'Pico one', variant: 'SPOTMICRO_ESP32_MINI' }

describe('robots', () => {
    beforeEach(() => localStorage.clear())

    it('keeps robots saved in the address-only format', async () => {
        const { robots } = await loadRobots([
            { address: '192.168.1.40', name: 'Leika', lastSeenAt: 5 }
        ])

        expect(get(robots)).toEqual([
            {
                id: null,
                name: 'Leika',
                variant: null,
                addresses: ['192.168.1.40'],
                lastAddress: '192.168.1.40',
                lastSeenAt: 5
            }
        ])
    })

    it('turns an address added by hand into the robot that answers there', async () => {
        const { robots, addRobot, identify } = await loadRobots()
        addRobot('192.168.1.40')

        identify('192.168.1.40', pico)

        expect(get(robots)).toHaveLength(1)
        expect(get(robots)[0]).toMatchObject({
            id: pico.deviceId,
            name: 'Pico one',
            variant: 'SPOTMICRO_ESP32_MINI',
            addresses: ['192.168.1.40']
        })
    })

    it('merges the same robot reached by IP and by mDNS name into one entry', async () => {
        const { robots, addRobot, identify } = await loadRobots()
        addRobot('192.168.1.40')
        identify('192.168.1.40', pico)
        addRobot('spot-micro-0ba1f3.local')

        identify('spot-micro-0ba1f3.local', pico)

        expect(get(robots)).toHaveLength(1)
        expect(get(robots)[0].addresses).toEqual(['192.168.1.40', 'spot-micro-0ba1f3.local'])
        expect(get(robots)[0].lastAddress).toBe('spot-micro-0ba1f3.local')
    })

    it('moves an address to the robot now answering there', async () => {
        const { robots, identify } = await loadRobots()
        identify('192.168.1.40', pico)

        const yertle = { deviceId: 'A4CF12000001', name: 'Yertle', variant: 'SPOTMICRO_YERTLE' }
        identify('192.168.1.40', yertle)

        expect(get(robots).map(robot => [robot.name, robot.addresses])).toEqual([
            ['Pico one', []],
            ['Yertle', ['192.168.1.40']]
        ])
    })

    it('leaves the entry unidentified when older firmware sends no device id', async () => {
        const { robots, addRobot, identify } = await loadRobots()
        addRobot('192.168.1.40')

        identify('192.168.1.40', { deviceId: '', name: '', variant: 'SPOTMICRO_ESP32' })

        expect(get(robots)).toEqual([
            expect.objectContaining({ id: null, name: '192.168.1.40', variant: 'SPOTMICRO_ESP32' })
        ])
    })

    it('lists every known address once for discovery', async () => {
        const { addRobot, identify, savedAddresses } = await loadRobots()
        identify('192.168.1.40', pico)
        identify('spot-micro-0ba1f3.local', pico)
        addRobot('192.168.1.41')

        expect(savedAddresses()).toEqual([
            '192.168.1.40',
            'spot-micro-0ba1f3.local',
            '192.168.1.41'
        ])
    })

    it('forgets a robot with all of its addresses', async () => {
        const { robots, identify, forgetRobot } = await loadRobots()
        identify('192.168.1.40', pico)
        identify('spot-micro-0ba1f3.local', pico)

        forgetRobot(get(robots)[0])

        expect(get(robots)).toEqual([])
    })
})
