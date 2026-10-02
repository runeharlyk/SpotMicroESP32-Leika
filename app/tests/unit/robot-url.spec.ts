import { describe, it, expect, afterEach } from 'vitest'
import {
    apiLocation,
    canReachRobot,
    robotHttpUrl,
    robotSocketUrl
} from '../../src/lib/stores/location-store'

describe('robot URLs', () => {
    afterEach(() => apiLocation.set(''))

    it('keeps paths relative when the app is served by the robot itself', () => {
        expect(robotHttpUrl('/api/camera/stream')).toBe('/api/camera/stream')
    })

    it('points at the saved robot address with an explicit scheme', () => {
        apiLocation.set('192.168.1.5')
        expect(robotHttpUrl('/api/camera/stream')).toBe('http://192.168.1.5/api/camera/stream')
    })

    it('leaves absolute URLs untouched', () => {
        apiLocation.set('192.168.1.5')
        expect(robotHttpUrl('http://other/api')).toBe('http://other/api')
    })

    it('opens the socket on the page host when no robot address is saved', () => {
        expect(robotSocketUrl()).toBe(`ws://${window.location.host}/api/ws`)
    })

    it('opens the socket on the saved robot address', () => {
        apiLocation.set('spot.local')
        expect(robotSocketUrl()).toBe('ws://spot.local/api/ws')
    })

    it('opens the socket on an explicitly given address over the saved one', () => {
        apiLocation.set('spot.local')
        expect(robotSocketUrl('10.0.0.7')).toBe('ws://10.0.0.7/api/ws')
    })
})

describe('canReachRobot', () => {
    const hosted = new URL('https://runeharlyk.github.io/SpotMicroESP32-Leika/')

    it('reaches the robot that serves the app itself', () => {
        expect(canReachRobot(new URL('http://spot-micro.local/'), '')).toBe(true)
    })

    it('has no robot to reach from the hosted app until an address is saved', () => {
        expect(canReachRobot(hosted, '')).toBe(false)
    })

    it('reaches a saved robot from the hosted app', () => {
        expect(canReachRobot(hosted, '192.168.1.5')).toBe(true)
    })
})
