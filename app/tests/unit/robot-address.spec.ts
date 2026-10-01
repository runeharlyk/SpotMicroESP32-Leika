import { describe, it, expect } from 'vitest'
import { normalizeRobotAddress } from '../../src/lib/services/discovery'

// What people paste is usually a URL copied from the address bar; the app needs the host and port alone.
describe('a robot address typed by hand', () => {
    it('keeps a bare address or host name', () => {
        expect(normalizeRobotAddress('192.168.1.39')).toBe('192.168.1.39')
        // Host names are case-insensitive; the address bar writes them in lower case too.
        expect(normalizeRobotAddress('spot-micro-27CB70.local')).toBe('spot-micro-27cb70.local')
    })

    it('drops the scheme and anything after the host', () => {
        expect(normalizeRobotAddress('http://192.168.1.39/')).toBe('192.168.1.39')
        expect(normalizeRobotAddress('https://192.168.1.39')).toBe('192.168.1.39')
        expect(normalizeRobotAddress('ws://192.168.1.39/api/ws')).toBe('192.168.1.39')
        expect(normalizeRobotAddress('192.168.1.39/settings?x=1#top')).toBe('192.168.1.39')
    })

    it('keeps a port', () => {
        expect(normalizeRobotAddress('http://localhost:5173/')).toBe('localhost:5173')
    })

    it('ignores surrounding spaces and refuses what has no host', () => {
        expect(normalizeRobotAddress('  192.168.1.39  ')).toBe('192.168.1.39')
        expect(normalizeRobotAddress('http://')).toBeNull()
        expect(normalizeRobotAddress('   ')).toBeNull()
        expect(normalizeRobotAddress('robot one')).toBeNull()
    })
})
