import type { WifiNetwork } from '$lib/platform_shared/api'
import { ipToUint32, uint32ToIp } from '$lib/utilities'

// The rules the radio and the network impose on WiFi and access point settings, and the conversion
// between the robot's messages (addresses as numbers) and the forms that edit them (text).

const utf8Length = (text: string) => new TextEncoder().encode(text).length

/** 802.11 allows SSIDs of 1 to 32 bytes. */
export function ssidError(ssid: string): string | undefined {
    const bytes = utf8Length(ssid)
    if (bytes === 0) return 'Enter the network name'
    if (bytes > 32) return 'A network name has at most 32 bytes'
}

/** What the robot shows for a saved password, and keeps it on when it is sent back unchanged. */
export const STORED_PASSWORD_MASK = '********'

/** WPA2 takes 8 to 63 characters, or a 64-digit hex key; an empty password means an open network. */
export function passphraseError(password: string): string | undefined {
    if (password === '' || /^[0-9a-fA-F]{64}$/.test(password)) return
    if (password.length < 8 || password.length > 63)
        return 'Use 8 to 63 characters, or leave it empty for an open network'
}

const OCTET = '(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)'
const IPV4 = new RegExp(`^${OCTET}(\\.${OCTET}){3}$`)

export function ipError(text: string, { optional = false } = {}): string | undefined {
    if (optional && text === '') return
    if (!IPV4.test(text)) return 'Enter an IPv4 address, such as 192.168.1.10'
}

/** A mask's ones must be contiguous from the top, and there must be at least one. */
export function subnetMaskError(text: string): string | undefined {
    const error = ipError(text)
    if (error) return error
    const mask = text.split('.').reduce((bits, octet) => (bits << 8) | Number(octet), 0) >>> 0
    const inverted = ~mask >>> 0
    if (mask === 0 || (inverted & (inverted + 1)) !== 0)
        return 'Enter a subnet mask, such as 255.255.255.0'
}

/** A DNS label: letters, digits and hyphens, not at either end; the firmware stores up to 32. */
export function hostnameError(hostname: string): string | undefined {
    if (!/^[A-Za-z0-9]([A-Za-z0-9-]{0,30}[A-Za-z0-9])?$/.test(hostname))
        return 'Use 1 to 32 letters, digits and hyphens, not starting or ending with a hyphen'
}

/** A network as its editor shows it: addresses as text, empty when unset. */
export interface NetworkDraft {
    ssid: string
    password: string
    staticIp: boolean
    localIp: string
    gatewayIp: string
    subnetMask: string
    dnsIp1: string
    dnsIp2: string
}

export type NetworkErrors = Partial<Record<keyof NetworkDraft, string>>

const addressText = (address: number) => (address ? uint32ToIp(address) : '')

export function draftFromNetwork(network: WifiNetwork): NetworkDraft {
    return {
        ssid: network.ssid,
        password: network.password,
        staticIp: network.staticIpConfig,
        localIp: addressText(network.localIp),
        gatewayIp: addressText(network.gatewayIp),
        subnetMask: addressText(network.subnetMask),
        dnsIp1: addressText(network.dnsIp1),
        dnsIp2: addressText(network.dnsIp2)
    }
}

export function networkFromDraft(draft: NetworkDraft): WifiNetwork {
    const address = (text: string) => (draft.staticIp && text ? ipToUint32(text) : 0)
    return {
        ssid: draft.ssid,
        password: draft.password,
        staticIpConfig: draft.staticIp,
        localIp: address(draft.localIp),
        gatewayIp: address(draft.gatewayIp),
        subnetMask: address(draft.subnetMask),
        dnsIp1: address(draft.dnsIp1),
        dnsIp2: address(draft.dnsIp2)
    }
}

const withoutUndefined = <T extends object>(errors: T) =>
    Object.fromEntries(Object.entries(errors).filter(([, error]) => error !== undefined)) as T

/** The draft's errors by field; `saved` are the stored networks, `editing` the one the draft replaces. */
export function networkErrors(
    draft: NetworkDraft,
    saved: WifiNetwork[],
    editing?: WifiNetwork
): NetworkErrors {
    const duplicate = saved.some(network => network !== editing && network.ssid === draft.ssid)
    const keepsSavedPassword =
        editing?.password === STORED_PASSWORD_MASK && editing.ssid === draft.ssid
    return withoutUndefined({
        ssid: ssidError(draft.ssid) ?? (duplicate ? 'This network is saved already' : undefined),
        password:
            draft.password === STORED_PASSWORD_MASK && !keepsSavedPassword ?
                "Enter this network's password: the saved one stays with the network it was saved for"
            :   passphraseError(draft.password),
        ...(draft.staticIp && {
            localIp: ipError(draft.localIp),
            gatewayIp: ipError(draft.gatewayIp),
            subnetMask: subnetMaskError(draft.subnetMask),
            dnsIp1: ipError(draft.dnsIp1),
            dnsIp2: ipError(draft.dnsIp2, { optional: true })
        })
    })
}

export interface AccessPointDraft {
    ssid: string
    password: string
    channel: number
    maxClients: number
    localIp: string
    gatewayIp: string
    subnetMask: string
}

export function accessPointErrors(
    draft: AccessPointDraft
): Partial<Record<keyof AccessPointDraft, string>> {
    return withoutUndefined({
        ssid: ssidError(draft.ssid),
        password: passphraseError(draft.password),
        channel:
            Number.isInteger(draft.channel) && draft.channel >= 1 && draft.channel <= 13 ?
                undefined
            :   'Use a channel from 1 to 13',
        maxClients:
            Number.isInteger(draft.maxClients) && draft.maxClients >= 1 && draft.maxClients <= 8 ?
                undefined
            :   'Allow 1 to 8 clients',
        localIp: ipError(draft.localIp),
        gatewayIp: ipError(draft.gatewayIp),
        subnetMask: subnetMaskError(draft.subnetMask)
    })
}
