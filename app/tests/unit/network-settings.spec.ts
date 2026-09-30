import { describe, it, expect } from 'vitest'
import {
    accessPointErrors,
    draftFromNetwork,
    hostnameError,
    ipError,
    networkErrors,
    networkFromDraft,
    passphraseError,
    ssidError,
    subnetMaskError
} from '../../src/lib/network-settings'
import { WifiNetwork } from '../../src/lib/platform_shared/api'
import { ipToUint32 } from '../../src/lib/utilities'

describe('network settings rules', () => {
    it('accepts any SSID of 1 to 32 bytes, counting UTF-8 bytes', () => {
        expect(ssidError('A')).toBeUndefined()
        expect(ssidError('x'.repeat(32))).toBeUndefined()
        expect(ssidError('')).toBeDefined()
        expect(ssidError('x'.repeat(33))).toBeDefined()
        // 11 characters of 3 bytes each: 33 bytes, one too many for the radio
        expect(ssidError('ネットワークネットワーク'.slice(0, 11))).toBeDefined()
    })

    it('accepts an empty passphrase (open network), 8 to 63 characters, or a 64-digit hex key', () => {
        expect(passphraseError('')).toBeUndefined()
        expect(passphraseError('12345678')).toBeUndefined()
        expect(passphraseError('x'.repeat(63))).toBeUndefined()
        expect(passphraseError('ab'.repeat(32))).toBeUndefined()
        expect(passphraseError('1234567')).toBeDefined()
        expect(passphraseError('x'.repeat(64))).toBeDefined()
    })

    it('accepts only whole dotted-quad addresses', () => {
        expect(ipError('192.168.1.10')).toBeUndefined()
        for (const bad of [
            '',
            '192.168.1',
            '1.1.1.1.1',
            '10.0.0.1x',
            '256.1.1.1',
            '01.2.3.4',
            ' 1.2.3.4'
        ])
            expect(ipError(bad), bad).toBeDefined()
        expect(ipError('', { optional: true })).toBeUndefined()
    })

    it('accepts only subnet masks whose ones are contiguous', () => {
        expect(subnetMaskError('255.255.255.0')).toBeUndefined()
        expect(subnetMaskError('255.255.254.0')).toBeUndefined()
        expect(subnetMaskError('255.0.255.0')).toBeDefined()
        expect(subnetMaskError('0.0.0.0')).toBeDefined()
    })

    it('accepts hostnames a network can resolve: letters, digits and inner hyphens, up to 32', () => {
        expect(hostnameError('spot-micro-27CB70')).toBeUndefined()
        for (const bad of ['', '-spot', 'spot-', 'spot micro', 'spot_micro', 'x'.repeat(33)])
            expect(hostnameError(bad), bad).toBeDefined()
    })
})

describe('the network editor', () => {
    const staticNetwork = WifiNetwork.create({
        ssid: 'Office',
        password: 'secret-123',
        staticIpConfig: true,
        localIp: ipToUint32('192.168.1.50'),
        gatewayIp: ipToUint32('192.168.1.1'),
        subnetMask: ipToUint32('255.255.255.0'),
        dnsIp1: ipToUint32('1.1.1.1')
    })

    it('round-trips a network with a static address, leaving an unset DNS 2 empty', () => {
        const draft = draftFromNetwork(staticNetwork)
        expect(draft.staticIp).toBe(true)
        expect(draft.localIp).toBe('192.168.1.50')
        expect(draft.dnsIp2).toBe('')
        expect(networkFromDraft(draft)).toEqual(staticNetwork)
    })

    it('checks the addresses only for a static configuration, and DNS 2 is optional', () => {
        const dhcp = {
            ...draftFromNetwork(WifiNetwork.create({ ssid: 'Home' })),
            localIp: 'nonsense'
        }
        expect(networkErrors(dhcp, [])).toEqual({})
        const draft = draftFromNetwork(staticNetwork)
        expect(networkErrors(draft, [])).toEqual({})
        expect(networkErrors({ ...draft, subnetMask: '255.0.255.0' }, [])).toHaveProperty(
            'subnetMask'
        )
    })

    it('refuses a second network with the same SSID, but not the one being edited', () => {
        const draft = draftFromNetwork(staticNetwork)
        expect(networkErrors(draft, [staticNetwork])).toHaveProperty('ssid')
        expect(networkErrors(draft, [staticNetwork], staticNetwork)).toEqual({})
    })

    it('checks every access point field', () => {
        const good = {
            ssid: 'Spot-Micro',
            password: 'spot-leika',
            channel: 6,
            maxClients: 4,
            localIp: '192.168.4.1',
            gatewayIp: '192.168.4.1',
            subnetMask: '255.255.255.0'
        }
        expect(accessPointErrors(good)).toEqual({})
        expect(
            Object.keys(
                accessPointErrors({ ...good, password: 'short', channel: 14, maxClients: 9 })
            ).sort()
        ).toEqual(['channel', 'maxClients', 'password'])
    })
})
