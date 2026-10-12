import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Sonar from '../../src/routes/peripherals/imu/Sonar.svelte'
import { sonarText } from '../../src/lib/sonar'
import { socket } from '../../src/lib/stores/socket'
import { SonarData, SonarStatus } from '../../src/lib/platform_shared/message'

describe('sonar reading text', () => {
    it('shows a distance in whole centimetres', () => {
        expect(sonarText({ status: SonarStatus.SONAR_DISTANCE, distanceCm: 99.6 })).toBe('100 cm')
    })

    it('tells no echo and no sensor from a distance, whatever distance came along', () => {
        expect(sonarText({ status: SonarStatus.SONAR_NO_ECHO, distanceCm: 0 })).toBe(
            'No echo within 4 m'
        )
        expect(sonarText({ status: SonarStatus.SONAR_NO_SENSOR, distanceCm: 12 })).toBe(
            'No sensor answers'
        )
    })

    it('shows a sonar that is off, or that the robot left out, as off', () => {
        expect(sonarText({ status: SonarStatus.SONAR_OFF, distanceCm: 0 })).toBe('Off')
        expect(sonarText(undefined)).toBe('Off')
    })
})

describe('Sonar card', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        vi.restoreAllMocks()
        document.body.innerHTML = ''
    })

    it('streams the sonars while shown and shows each side', () => {
        let deliver: ((data: SonarData) => void) | undefined
        const unsubscribe = vi.fn()
        const on = vi.spyOn(socket, 'on').mockImplementation((type, listener) => {
            if (type === SonarData) deliver = listener as (data: SonarData) => void
            return unsubscribe
        })
        component = mount(Sonar, { target: document.body })
        flushSync()

        expect(on).toHaveBeenCalledWith(SonarData, expect.any(Function))
        const side = (name: string) =>
            document.body.querySelector(`[data-testid="sonar-${name}"]`)!.textContent!.trim()
        expect(side('left')).toBe('Waiting')

        deliver!(
            SonarData.create({
                left: { status: SonarStatus.SONAR_DISTANCE, distanceCm: 42.2 },
                right: { status: SonarStatus.SONAR_NO_SENSOR, distanceCm: 0 }
            })
        )
        flushSync()
        expect(side('left')).toBe('42 cm')
        expect(side('right')).toBe('No sensor answers')

        deliver!(
            SonarData.create({
                left: { status: SonarStatus.SONAR_NO_ECHO, distanceCm: 0 },
                right: { status: SonarStatus.SONAR_DISTANCE, distanceCm: 150 }
            })
        )
        flushSync()
        expect(side('left')).toBe('No echo within 4 m')
        expect(side('right')).toBe('150 cm')

        unmount(component)
        component = undefined
        expect(unsubscribe).toHaveBeenCalledOnce()
    })
})
