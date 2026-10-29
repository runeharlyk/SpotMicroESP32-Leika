// watch() over the real socket and a robot on a WebSocket that answers a subscribe as the firmware does (topic-robot.ts):
// a state topic's value, then the ack. The watch shows the value and whether it is live, stale or failed; the link's
// drops, the robot's lost frames and firmware from before the topics are what it has to survive.
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { Readable } from 'svelte/store'
import { GoalStatus, ModeData, ModesEnum } from '../../src/lib/platform_shared/message'
import { createWebSocket, MESSAGE_TYPE_TO_TAG } from '../../src/lib/stores/socket'
import { watch } from '../../src/lib/robot/watch'
import { describeWatchFailure, SUBSCRIBE_TRIES, type Watched } from '../../src/lib/transport/topics'
import { TopicRobot } from './topic-robot'

const ACK_TIMEOUT_MS = 150
const MODE = MESSAGE_TYPE_TO_TAG.get(ModeData as never)!
const GOAL = MESSAGE_TYPE_TO_TAG.get(GoalStatus as never)!
const { STAND, WALK } = ModesEnum

const record = <T>(store: Readable<Watched<T>>) => {
    const seen: Watched<T>[] = []
    const stop = store.subscribe(w => seen.push(w))
    return { seen, stop, now: () => seen[seen.length - 1] }
}

describe.sequential('watch', () => {
    let robot: TopicRobot
    let socket: ReturnType<typeof createWebSocket>

    beforeEach(async () => {
        robot = new TopicRobot()
        socket = createWebSocket({ ackTimeoutTime: ACK_TIMEOUT_MS })
        const opened = new Promise(resolve => socket.onEvent('open', resolve))
        socket.init(await robot.start())
        await opened
    })

    afterEach(() => robot.stop())

    it('shows the snapshot as it arrives and goes live on the ack', async () => {
        robot.set(ModeData, { mode: WALK })
        const mode = record(watch(ModeData, socket))

        await vi.waitFor(() => expect(mode.now().status).toBe('live'))

        expect(mode.seen.map(w => [w.value?.mode, w.status])).toEqual([
            [undefined, 'subscribing'],
            [WALK, 'subscribing'],
            [WALK, 'live']
        ])
        mode.stop()
    })

    it('subscribes once for every watch of a tag, and a second watch starts live with the value', async () => {
        robot.set(ModeData, { mode: STAND })
        const first = record(watch(ModeData, socket))
        await vi.waitFor(() => expect(first.now().status).toBe('live'))

        const second = record(watch(ModeData, socket))

        expect(robot.subscriptions()).toEqual([MODE])
        expect(second.seen).toHaveLength(1)
        expect(second.now()).toMatchObject({ value: { mode: STAND }, status: 'live' })
        first.stop()
        second.stop()
    })

    it('follows a push after the ack', async () => {
        const mode = record(watch(ModeData, socket))
        await vi.waitFor(() => expect(mode.now().status).toBe('live'))

        robot.set(ModeData, { mode: STAND })

        await vi.waitFor(() => expect(mode.now().value?.mode).toBe(STAND))
        expect(mode.now().status).toBe('live')
        mode.stop()
    })

    // An ack with no value before it means the snapshot was lost on the way.
    it('subscribes again when the snapshot was lost, and fails after as many acks without one as it tries', async () => {
        robot.dropNext(MODE, 1)
        const mode = record(watch(ModeData, socket))
        await vi.waitFor(() => expect(mode.now().status).toBe('live'))
        expect(robot.subscriptions()).toEqual([MODE, MODE])
        mode.stop()

        robot.dropNext(GOAL, SUBSCRIBE_TRIES)
        const goal = record(watch(GoalStatus, socket))
        await vi.waitFor(() => expect(goal.now().status).toBe('error'))
        expect(robot.subscriptions().filter(tag => tag === GOAL)).toHaveLength(SUBSCRIBE_TRIES)
        expect(goal.now()).toMatchObject({ value: undefined, failure: 'timeout' })
        goal.stop()
    })

    it('subscribes again when no ack comes in time, and gives up after its tries', async () => {
        robot.stall()
        const mode = record(watch(ModeData, socket))

        await vi.waitFor(() => expect(mode.now().status).toBe('error'))

        expect(robot.subscriptions()).toEqual(Array(SUBSCRIBE_TRIES).fill(MODE))
        expect(mode.now().failure).toBe('timeout')
        mode.stop()
    })

    // Firmware from before the topics answers pings and pushes changes, but never acks a subscribe. The watch keeps
    // what the pushes bring and says the firmware is too old, rather than that the robot does not answer.
    it('names a firmware that carries frames but never acks as too old, and keeps its pushes', async () => {
        robot.ackless()
        const mode = record(watch(ModeData, socket))
        await vi.waitFor(() => expect(robot.subscriptions()).toHaveLength(1))
        robot.set(ModeData, { mode: STAND })

        await vi.waitFor(() => expect(mode.now().status).toBe('error'))

        expect(mode.now()).toMatchObject({ value: { mode: STAND }, failure: 'too-old' })
        expect(describeWatchFailure('too-old', 'its mode')).toBe(
            "The robot's firmware does not report its mode; update it"
        )
        mode.stop()
    })

    it('keeps the value as stale while the link is down, and the next snapshot makes it current', async () => {
        robot.set(ModeData, { mode: STAND })
        const mode = record(watch(ModeData, socket))
        await vi.waitFor(() => expect(mode.now().status).toBe('live'))
        const reopened = new Promise(resolve => socket.onEvent('open', resolve))

        robot.drop()
        await vi.waitFor(() => expect(mode.now().stale).toBe(true))
        expect(mode.now()).toMatchObject({ value: { mode: STAND }, status: 'subscribing' })
        robot.set(ModeData, { mode: WALK })
        await reopened

        await vi.waitFor(() => expect(mode.now().status).toBe('live'))
        expect(mode.now()).toMatchObject({ value: { mode: WALK }, stale: false })
        expect(robot.subscriptions()).toEqual([MODE, MODE])
        mode.stop()
    })

    it('forgets the value when another robot is chosen, and asks the one reached', async () => {
        robot.set(ModeData, { mode: STAND })
        const mode = record(watch(ModeData, socket))
        await vi.waitFor(() => expect(mode.now().status).toBe('live'))
        const other = new TopicRobot()
        other.set(ModeData, { mode: WALK })

        socket.init(await other.start())

        expect(mode.now()).toMatchObject({ value: undefined, stale: false })
        await vi.waitFor(() => expect(mode.now().status).toBe('live'))
        expect(mode.now().value?.mode).toBe(WALK)
        mode.stop()
        await other.stop()
    })

    it('unsubscribes once the last watch stops, and a new watch starts afresh', async () => {
        const first = record(watch(ModeData, socket))
        const second = record(watch(ModeData, socket))
        await vi.waitFor(() => expect(second.now().status).toBe('live'))

        first.stop()
        second.stop()

        await vi.waitFor(() =>
            expect(robot.received.filter(m => m.unsubNotif).map(m => m.unsubNotif!.tag)).toEqual([
                MODE
            ])
        )
        const again = record(watch(ModeData, socket))
        expect(again.now()).toMatchObject({ value: undefined, status: 'subscribing' })
        await vi.waitFor(() => expect(again.now().status).toBe('live'))
        again.stop()
    })
})
