import { untrack } from 'svelte'
import { derived, readable, type Readable } from 'svelte/store'
import type { FeaturesDataResponse } from '$lib/platform_shared/message'
import { connectionFeatures } from '$lib/stores/featureFlags'
import { socket } from '$lib/stores/socket'
import {
    describeWatchFailure,
    type SettingsTopic,
    type ValueOf,
    type Watched
} from '$lib/transport/topics'
import { watch, type Follower } from './watch'

const WAITING: Watched<never> = {
    value: undefined,
    status: 'subscribing',
    stale: false,
    failure: null
}
const TOO_OLD: Watched<never> = { ...WAITING, status: 'error', failure: 'too-old' }

/** Where a settings group is read from: the robot's features, and the socket that follows its topic. */
export interface SettingsLink {
    features: Readable<FeaturesDataResponse | null>
    from: Follower
}

const LIVE: SettingsLink = { features: connectionFeatures, from: socket }

/**
 * A settings group as the robot stores it, from its state topic: the stored value and every stored change, whichever
 * client or task made it. The topic is followed once the robot's features are in and say it serves the settings topics;
 * firmware from before them is named as too old and not subscribed to, since it would answer nothing.
 */
export const storedSettings = <F extends SettingsTopic>(
    topic: F,
    { features, from }: SettingsLink = LIVE
): Readable<Watched<ValueOf<F>>> => {
    const followed = watch(topic, from)
    const gate = derived(features, $features =>
        $features === null ? 'waiting'
        : $features.settingsTopics ? 'follow'
        : 'too-old'
    )
    return readable<Watched<ValueOf<F>>>(WAITING, set => {
        let unfollow: (() => void) | null = null
        const stopGate = gate.subscribe(reading => {
            if (reading === 'follow') return void (unfollow ??= followed.subscribe(set))
            unfollow?.()
            unfollow = null
            set(reading === 'waiting' ? WAITING : TOO_OLD)
        })
        return () => {
            stopGate()
            unfollow?.()
        }
    })
}

/**
 * A settings group read once, as stored now: for a reader that wants no pushes after, such as the configuration
 * export. Rejects as the watch fails, and when no value is live within `timeoutMs`.
 */
export const settingsOnce = <F extends SettingsTopic>(
    topic: F,
    what: string,
    { link = LIVE, timeoutMs = 10000 }: { link?: SettingsLink; timeoutMs?: number } = {}
): Promise<ValueOf<F>> => {
    let stop = () => {}
    let timer: ReturnType<typeof setTimeout> | undefined
    const read = new Promise<ValueOf<F>>((resolve, reject) => {
        timer = setTimeout(() => reject(new Error(`The robot did not report ${what}`)), timeoutMs)
        stop = storedSettings(topic, link).subscribe(({ value, status, failure }) => {
            if (status === 'live' && value !== undefined) resolve(value)
            else if (status === 'error' && failure)
                reject(new Error(describeWatchFailure(failure, what)))
        })
    })
    return read.finally(() => {
        clearTimeout(timer)
        queueMicrotask(stop)
    })
}

const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b)

/**
 * A settings form over a group's topic. `stored` is the group as the robot stores it, and `draft` the form's copy of
 * it, made by `toDraft`. The draft takes each stored value while the user has not changed it; once changed, a stored
 * change from elsewhere leaves it, and `changedOnRobot` says so until the draft matches the stored value again, as
 * after a save. `saving` holds while a write is under way. Call `follow` from an effect.
 */
export class SettingsForm<F extends SettingsTopic, D> {
    watched = $state.raw<Watched<ValueOf<F>>>(WAITING)
    stored = $state.raw<ValueOf<F> | null>(null)
    draft = $state<D | null>(null)
    changedOnRobot = $state(false)
    saving = $state(false)
    private taken: D | null = null

    constructor(
        private readonly topic: F,
        private readonly toDraft: (stored: ValueOf<F>) => D,
        private readonly link: SettingsLink = LIVE
    ) {}

    /** Follows the topic; returns the function that stops. What it reads leaves the calling effect untracked. */
    follow(): () => void {
        return untrack(() =>
            storedSettings(this.topic, this.link).subscribe(watched => {
                this.watched = watched
                if (watched.value === undefined) return
                this.take(watched.value)
            })
        )
    }

    /** What the page says while it has no settings to show: null while it waits for them. */
    get problem(): string | null {
        const { status, failure } = this.watched
        return status === 'error' && failure ?
                describeWatchFailure(failure, 'these settings')
            :   null
    }

    /** Runs a write, `saving` while it is under way; its reply is what the caller reads, and its push the form takes. */
    async save<T>(write: () => Promise<T>): Promise<T> {
        this.saving = true
        try {
            return await write()
        } finally {
            this.saving = false
        }
    }

    /** Puts the draft back to the stored value. */
    reset() {
        if (this.stored) this.take(this.stored, true)
    }

    private take(stored: ValueOf<F>, force = false) {
        const next = this.toDraft(structuredClone(stored))
        const untouched = this.draft === null || same($state.snapshot(this.draft), this.taken)
        this.stored = stored
        if (force || untouched) this.draft = next
        this.changedOnRobot = !force && !untouched && !same($state.snapshot(this.draft), next)
        this.taken = next
    }
}
