import { get } from 'svelte/store'
import { persistentStore } from '$lib/utilities'

export interface Robot {
    /** Factory MAC reported by the robot; null until the robot has answered at one of its addresses. */
    id: string | null
    name: string
    variant: string | null
    addresses: string[]
    lastAddress: string
    lastSeenAt: number | null
}

export interface RobotIdentity {
    deviceId: string
    name: string
    variant: string
    hostname: string
}

/**
 * The robot's mDNS name, when it is provably this board's: the factory default embeds the last
 * six hex digits of the device id, whereas a name like "spot-micro" may be shared by several robots.
 */
const ownMdnsName = ({ deviceId, hostname }: RobotIdentity) =>
    hostname && hostname.toLowerCase().includes(deviceId.slice(-6).toLowerCase()) ?
        `${hostname.toLowerCase()}.local`
    :   null

type AddressOnlyRobot = { address: string; name: string; lastSeenAt: number | null }

const upgrade = (saved: Robot | AddressOnlyRobot): Robot =>
    'address' in saved ?
        {
            id: null,
            name: saved.name,
            variant: null,
            addresses: [saved.address],
            lastAddress: saved.address,
            lastSeenAt: saved.lastSeenAt
        }
    :   saved

export const robots = persistentStore<Robot[]>('robots', [])
robots.update(list => (list as (Robot | AddressOnlyRobot)[]).map(upgrade))

/** Default third octet to sweep, remembered so the next sweep starts where the last one did. */
export const subnetPrefix = persistentStore('subnet_prefix', '192.168.1.')

export const robotKey = (robot: Robot) => robot.id ?? robot.lastAddress

const VARIANT_LABELS: Record<string, string> = {
    SPOTMICRO_ESP32: 'Spot Micro',
    SPOTMICRO_ESP32_MINI: 'Spot Micro Mini',
    SPOTMICRO_YERTLE: 'Yertle'
}

export const variantLabel = (variant: string | null) =>
    variant ? (VARIANT_LABELS[variant.replace(/_V\d+$/, '')] ?? variant) : null

export const savedAddresses = () => [...new Set(get(robots).flatMap(robot => robot.addresses))]

export const addRobot = (address: string) => {
    if (savedAddresses().includes(address)) return
    robots.update(list => [
        ...list,
        {
            id: null,
            name: address,
            variant: null,
            addresses: [address],
            lastAddress: address,
            lastSeenAt: null
        }
    ])
}

const withoutAddress = (robot: Robot, address: string): Robot => {
    const addresses = robot.addresses.filter(known => known !== address)
    const lastAddress = robot.lastAddress === address ? (addresses[0] ?? '') : robot.lastAddress
    return { ...robot, addresses, lastAddress }
}

/**
 * Records which robot answered at an address. The address leaves any robot that held it before,
 * so an IP handed to another robot by DHCP follows the robot that now owns it.
 */
export const identify = (address: string, identity: RobotIdentity) => {
    if (!identity.deviceId) {
        robots.update(list =>
            list.map(robot =>
                robot.addresses.includes(address) ?
                    { ...robot, variant: identity.variant || robot.variant }
                :   robot
            )
        )
        return
    }

    // Addresses proven to reach this robot leave any entry that held them before.
    const proven = [address, ...[ownMdnsName(identity)].filter(name => name !== null)]

    robots.update(list => {
        const known = list.find(robot => robot.id === identity.deviceId)
        const identified: Robot = {
            id: identity.deviceId,
            name: identity.name || known?.name || address,
            variant: identity.variant || known?.variant || null,
            addresses: [
                ...(known?.addresses.filter(held => !proven.includes(held)) ?? []),
                ...proven
            ],
            lastAddress: address,
            lastSeenAt: Date.now()
        }

        let placed = false
        const next: Robot[] = []
        for (const robot of list) {
            const replacesThis =
                robot.id === identity.deviceId ||
                (robot.id === null && robot.addresses.some(held => proven.includes(held)))
            if (replacesThis) {
                if (!placed) next.push(identified)
                placed = true
                continue
            }
            const stripped = proven.reduce(withoutAddress, robot)
            if (stripped.id !== null || stripped.addresses.length) next.push(stripped)
        }
        if (!placed) next.push(identified)
        return next
    })
}

export const forgetRobot = (forgotten: Robot) =>
    robots.update(list => list.filter(robot => robotKey(robot) !== robotKey(forgotten)))

export const markSeen = (address: string) =>
    robots.update(list =>
        list.map(robot =>
            robot.addresses.includes(address) ? { ...robot, lastSeenAt: Date.now() } : robot
        )
    )
