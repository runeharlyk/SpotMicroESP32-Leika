import { describe, it, expect } from 'vitest'
import { get, writable } from 'svelte/store'
import { mirrorRobot } from '../../src/lib/robot-mirror'
import { ModeData, ModesEnum } from '../../src/lib/platform_shared/message'

const mode = (value: ModesEnum) => ModeData.create({ mode: value })
const sameMode = (a: ModeData, b: ModeData) => a.mode === b.mode

describe('a store the robot reports and the user changes', () => {
    it("sends the user's changes to the robot", () => {
        const store = writable(mode(ModesEnum.DEACTIVATED))
        const sent: ModesEnum[] = []
        mirrorRobot(store, value => sent.push(value.mode), sameMode)
        store.set(mode(ModesEnum.STAND))
        expect(sent).toContain(ModesEnum.STAND)
    })

    // The robot reports once a second: echoing that back would re-apply its mode and restart its gait.
    it("shows the robot's report without sending it back", () => {
        const store = writable(mode(ModesEnum.DEACTIVATED))
        const sent: ModesEnum[] = []
        const mirror = mirrorRobot(store, value => sent.push(value.mode), sameMode)
        sent.length = 0
        mirror.report(mode(ModesEnum.WALK))
        mirror.report(mode(ModesEnum.WALK))
        expect(get(store).mode).toBe(ModesEnum.WALK)
        expect(sent).toEqual([])
    })

    it('sends a change the user makes after a report', () => {
        const store = writable(mode(ModesEnum.DEACTIVATED))
        const sent: ModesEnum[] = []
        const mirror = mirrorRobot(store, value => sent.push(value.mode), sameMode)
        mirror.report(mode(ModesEnum.WALK))
        sent.length = 0
        store.set(mode(ModesEnum.REST))
        expect(sent).toEqual([ModesEnum.REST])
    })

    it('stops sending once stopped', () => {
        const store = writable(mode(ModesEnum.DEACTIVATED))
        const sent: ModesEnum[] = []
        const mirror = mirrorRobot(store, value => sent.push(value.mode), sameMode)
        mirror.stop()
        sent.length = 0
        store.set(mode(ModesEnum.STAND))
        expect(sent).toEqual([])
    })
})
