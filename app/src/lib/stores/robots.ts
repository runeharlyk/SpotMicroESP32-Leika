import { get } from 'svelte/store'
import { persistentStore } from '$lib/utilities'

export interface Robot {
    address: string
    name: string
    lastSeenAt: number | null
}

export const robots = persistentStore<Robot[]>('robots', [])

/** Default third octet to sweep, remembered so the next sweep starts where the last one did. */
export const subnetPrefix = persistentStore('subnet_prefix', '192.168.1.')

export const savedAddresses = () => get(robots).map(robot => robot.address)

export const addRobot = (address: string, name = address) => {
    if (get(robots).some(robot => robot.address === address)) return
    robots.update(list => [...list, { address, name, lastSeenAt: null }])
}

export const forgetRobot = (address: string) =>
    robots.update(list => list.filter(robot => robot.address !== address))

export const markSeen = (address: string) =>
    robots.update(list =>
        list.map(robot =>
            robot.address === address ? { ...robot, lastSeenAt: Date.now() } : robot
        )
    )
