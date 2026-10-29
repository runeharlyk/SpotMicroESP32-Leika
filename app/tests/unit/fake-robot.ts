import { vi } from 'vitest'
import { socket } from '../../src/lib/stores/socket'
import { CorrelationResponse, FeaturesDataResponse } from '../../src/lib/platform_shared/message'
import {
    APSettings,
    PeripheralSettings,
    ServoSettings,
    WifiSettings
} from '../../src/lib/platform_shared/api'
import { connectionFeatures } from '../../src/lib/stores/featureFlags'
import type { StateTopic, ValueOf, Watched, WatchFailure } from '../../src/lib/transport/topics'

type RequestData = Parameters<typeof socket.request>[0]
type Reply = Partial<CorrelationResponse>

// The settings writes, by request, and the topic each group is pushed on.
const SETTINGS_WRITES = {
    servoSettings: ServoSettings,
    peripheralSettings: PeripheralSettings,
    wifiSettings: WifiSettings,
    apSettings: APSettings
} as const

/**
 * Stands in for the robot at the socket: every request is answered by `answer`, keyed by the
 * request's field name (e.g. 'apStatusRequest'), and each sent request's name is recorded.
 * Replies default to status 200, as the firmware sends them. With `states`, a settings write the
 * robot takes is pushed on its group's topic first, as the firmware pushes a stored change before
 * it replies, with the settings the reply carries, else those written.
 */
export function fakeRobot(
    answer: (name: string, data: RequestData) => Reply | Promise<Reply>,
    { states }: { states?: ReturnType<typeof fakeStates> } = {}
) {
    const sent: string[] = []
    const spy = vi.spyOn(socket, 'request').mockImplementation(async data => {
        const name = Object.entries(data).find(([, value]) => value !== undefined)![0]
        sent.push(name)
        const reply = CorrelationResponse.create({
            correlationId: 1,
            statusCode: 200,
            ...(await answer(name, data))
        })
        if (states && reply.statusCode < 400 && name in SETTINGS_WRITES) {
            const key = name as keyof typeof SETTINGS_WRITES
            states.set(SETTINGS_WRITES[key], (reply[key] ?? data[key]) as never)
        }
        return reply
    })
    return { sent, restore: () => spy.mockRestore() }
}

/** The robot serves its settings as topics from `initial`, as its features say: undone by the returned function. */
export function servesSettings(states: ReturnType<typeof fakeStates>, initial: Partial<Groups>) {
    connectionFeatures.set(FeaturesDataResponse.create({ settingsTopics: true }))
    for (const [key, value] of Object.entries(initial))
        states.set(SETTINGS_WRITES[key as keyof Groups], value as never)
    return () => connectionFeatures.set(null)
}

type Groups = { [K in keyof typeof SETTINGS_WRITES]: ValueOf<(typeof SETTINGS_WRITES)[K]> }

type Watcher = (state: Watched<unknown>) => void

const SUBSCRIBING: Watched<unknown> = {
    value: undefined,
    status: 'subscribing',
    stale: false,
    failure: null
}

/**
 * Stands in for the robot's state topics at the socket: a follow is told the topic's state at once, live with the
 * value last `set`, and subscribing for a topic not set yet, and every `set` or `lose` after reaches its followers.
 */
export function fakeStates() {
    const states = new Map<unknown, Watched<unknown>>()
    const watchers = new Map<unknown, Set<Watcher>>()
    const spy = vi.spyOn(socket, 'follow').mockImplementation(((fns: unknown, watcher: Watcher) => {
        const followers = watchers.get(fns) ?? new Set<Watcher>()
        watchers.set(fns, followers)
        followers.add(watcher)
        watcher(states.get(fns) ?? SUBSCRIBING)
        return () => followers.delete(watcher)
    }) as unknown as typeof socket.follow)
    const tell = (topic: unknown, state: Watched<unknown>) => {
        states.set(topic, state)
        watchers.get(topic)?.forEach(watcher => watcher(state))
    }
    return {
        set: <F extends StateTopic>(topic: F, value: ValueOf<F>) =>
            tell(topic, { value, status: 'live', stale: false, failure: null }),
        /** The link dropped: the value is kept, as stale. */
        lose: (topic: StateTopic) =>
            tell(topic, {
                ...(states.get(topic) ?? SUBSCRIBING),
                status: 'subscribing',
                stale: true
            }),
        /** The watch gave up, as on firmware that never acks: `too-old`. */
        fail: (topic: StateTopic, failure: WatchFailure) =>
            tell(topic, { ...(states.get(topic) ?? SUBSCRIBING), status: 'error', failure }),
        following: (topic: StateTopic) => (watchers.get(topic)?.size ?? 0) > 0,
        restore: () => spy.mockRestore()
    }
}

/** A robot that serves its settings as topics, from `initial`, and answers requests with `answer` (fakeRobot). */
export function fakeSettingsRobot(
    initial: Partial<Groups>,
    answer: (name: string, data: RequestData) => Reply | Promise<Reply> = () => ({})
) {
    const states = fakeStates()
    const unserve = servesSettings(states, initial)
    const robot = fakeRobot(answer, { states })
    return {
        ...robot,
        states,
        restore: () => {
            robot.restore()
            states.restore()
            unserve()
        }
    }
}
