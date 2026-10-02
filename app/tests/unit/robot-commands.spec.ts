import { describe, it, expect, afterEach, vi } from 'vitest'
import { get } from 'svelte/store'
import { notifications } from '../../src/lib/components/toasts/notifications'
import { factoryResetRobot, restartRobot } from '../../src/lib/robot-commands'
import { fakeRobot } from './fake-robot'

// The notification store is module-global, so each test only inspects toasts raised after it starts.
const toastsSince = (earlier: number) => () => get(notifications).slice(earlier)

describe('robot commands', () => {
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => robot?.restore())

    it.each([
        ['restart', restartRobot, 'systemRestart'],
        ['factory reset', factoryResetRobot, 'systemReset']
    ])(
        '%s asks the robot over the socket and says it was accepted',
        async (_, command, request) => {
            robot = fakeRobot(() => ({}))
            const toasts = toastsSince(get(notifications).length)

            await command()

            expect(robot.sent).toEqual([request])
            expect(toasts().map(t => t.type)).toEqual(['success'])
        }
    )

    it('reports a restart the robot did not confirm', async () => {
        robot = fakeRobot(() => Promise.reject(new Error('Request timeout (id: 3)')))
        const toasts = toastsSince(get(notifications).length)

        await restartRobot()

        await vi.waitFor(() =>
            expect(toasts()).toEqual([
                expect.objectContaining({
                    type: 'error',
                    message: expect.stringMatching(/Restart failed.*timeout/)
                })
            ])
        )
    })
})
