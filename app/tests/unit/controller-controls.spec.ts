import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { get, readable } from 'svelte/store'
import Controls from '../../src/routes/controller/Controls.svelte'
import {
    input,
    mode,
    outControllerData,
    startingInput,
    walkGait
} from '../../src/lib/stores/model-store'
import { socket } from '../../src/lib/stores/socket'
import { fakeStates } from './fake-robot'
import { driveSimulation, simulation } from '../../src/lib/stores/drive'
import type { ClipPlayer } from '../../src/lib/simulation/robot-sim'
import {
    AnimationState,
    AnimationStatus,
    ModeData,
    ModesEnum,
    WalkGaitData,
    WalkGaits
} from '../../src/lib/platform_shared/message'

const byLabel = <T extends HTMLElement>(label: string) =>
    document.body.querySelector<T>(`[aria-label="${label}"]`)
const button = (text: string) =>
    [...document.body.querySelectorAll('button')].find(b => b.textContent?.trim() === text)
const slider = (label: string) => {
    const labelled = [...document.body.querySelectorAll('label')].find(l => l.textContent === label)
    return labelled && document.getElementById(labelled.htmlFor)
}
// jsdom does no layout: an element is shown unless it or a container is hidden.
const shown = (element: Element | null | undefined) => !!element && !element.closest('[hidden]')
const pick = (select: HTMLSelectElement, value: number) => {
    select.value = String(value)
    select.dispatchEvent(new Event('change', { bubbles: true }))
    flushSync()
}
const text = () => (document.body.textContent ?? '').replace(/\s+/g, ' ')

describe('controller panel', () => {
    let component: ReturnType<typeof mount> | undefined

    const open = (robotMode: ModesEnum, { linked = true } = {}) => {
        vi.spyOn(socket, 'subscribe').mockImplementation(readable(linked).subscribe)
        mode.set(ModeData.create({ mode: robotMode }))
        component = mount(Controls, { target: document.body })
        flushSync()
    }

    beforeEach(() => {
        walkGait.set(WalkGaitData.create({ gait: WalkGaits.TROT }))
        input.set(startingInput())
        outControllerData.set(startingInput())
        simulation.input.set(startingInput())
        simulation.mode.set(ModesEnum.DEACTIVATED)
        simulation.player.set(null)
        simulation.animationStatus.set(null)
    })

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        document.body.innerHTML = ''
        driveSimulation(false)
        vi.restoreAllMocks()
    })

    it('shows the mode select, Deactivate and the Adjustments toggle, with the sliders folded away', () => {
        open(ModesEnum.STAND)
        const modeSelect = byLabel<HTMLSelectElement>('Mode')!
        expect(shown(modeSelect)).toBe(true)
        expect(modeSelect.value).toBe(String(ModesEnum.STAND))
        expect(shown(button('Deactivate'))).toBe(true)
        const toggle = button('Adjustments')!
        expect(toggle.getAttribute('aria-expanded')).toBe('false')
        expect(shown(slider('Body height'))).toBe(false)

        toggle.click()
        flushSync()
        expect(toggle.getAttribute('aria-expanded')).toBe('true')
        expect(shown(slider('Body height'))).toBe(true)
        expect((slider('Body height') as HTMLInputElement).value).toBe('0.7')

        toggle.click()
        flushSync()
        expect(shown(slider('Body height'))).toBe(false)
    })

    it('keeps gait and speed at hand while walking and folds step height into the adjustments', () => {
        open(ModesEnum.WALK)
        expect(shown(byLabel('Gait'))).toBe(true)
        expect(shown(slider('Speed'))).toBe(true)
        expect(shown(slider('Step height'))).toBe(false)
        button('Adjustments')!.click()
        flushSync()
        expect(shown(slider('Step height'))).toBe(true)
        expect(shown(slider('Body height'))).toBe(true)
    })

    it('offers gait and speed only while walking', () => {
        open(ModesEnum.STAND)
        expect(byLabel('Gait')).toBeNull()
        expect(slider('Speed')).toBeUndefined()
        expect(slider('Step height')).toBeUndefined()
    })

    it('asks the robot for the picked mode and gait, and deactivates it directly', () => {
        open(ModesEnum.STAND)
        const modeSelect = byLabel<HTMLSelectElement>('Mode')!
        pick(modeSelect, ModesEnum.WALK)
        expect(get(mode).mode).toBe(ModesEnum.WALK)
        pick(byLabel<HTMLSelectElement>('Gait')!, WalkGaits.CRAWL)
        expect(get(walkGait).gait).toBe(WalkGaits.CRAWL)

        button('Deactivate')!.click()
        flushSync()
        expect(get(mode).mode).toBe(ModesEnum.DEACTIVATED)
        expect(modeSelect.value).toBe(String(ModesEnum.DEACTIVATED))
    })

    it('sends the robot the body height the slider is set to', () => {
        open(ModesEnum.STAND)
        const height = slider('Body height') as HTMLInputElement
        height.value = '0.25'
        height.dispatchEvent(new Event('input', { bubbles: true }))
        expect(get(input).height).toBe(0.25)
        expect(get(outControllerData).height).toBe(0.25)
    })

    it('shows a mode the robot reports but nobody picks, such as a clip playing', () => {
        open(ModesEnum.ANIMATE)
        const modeSelect = byLabel<HTMLSelectElement>('Mode')!
        expect(modeSelect.value).toBe(String(ModesEnum.ANIMATE))
        expect(modeSelect.selectedOptions[0].disabled).toBe(true)
    })

    it('drives the simulation alone while it has the controls, with no robot to deactivate', () => {
        open(ModesEnum.STAND)
        driveSimulation(true)
        flushSync()
        const modeSelect = byLabel<HTMLSelectElement>('Mode')!
        expect(modeSelect.value).toBe(String(ModesEnum.DEACTIVATED))
        expect(button('Deactivate')).toBeUndefined()
        expect(text()).toMatch('Simulation only')

        pick(modeSelect, ModesEnum.WALK)
        const speed = slider('Speed') as HTMLInputElement
        speed.value = '0.9'
        speed.dispatchEvent(new Event('input', { bubbles: true }))
        expect(get(simulation.mode)).toBe(ModesEnum.WALK)
        expect(get(simulation.input).speed).toBe(0.9)
        expect(get(mode).mode).toBe(ModesEnum.STAND)
        expect(get(outControllerData).speed).toBe(startingInput().speed)
    })

    it('offers clips standing and while one plays, and not while walking', () => {
        open(ModesEnum.STAND)
        expect(byLabel('Clip')).not.toBeNull()
        expect(
            [...byLabel<HTMLSelectElement>('Clip')!.options].map(option => option.value)
        ).toEqual(['bow', 'sit', 'wave'])
        mode.set(ModeData.create({ mode: ModesEnum.ANIMATE }))
        flushSync()
        expect(byLabel('Clip')).not.toBeNull()
        mode.set(ModeData.create({ mode: ModesEnum.WALK }))
        flushSync()
        expect(byLabel('Clip')).toBeNull()
    })

    it("plays a clip in the simulation and shows the simulation's clip playing", async () => {
        open(ModesEnum.DEACTIVATED, { linked: false })
        driveSimulation(true)
        simulation.mode.set(ModesEnum.STAND)
        flushSync()
        expect(button('Play')!.disabled).toBe(true)
        const player = {
            playClip: vi.fn<ClipPlayer['playClip']>(() => true),
            stopClip: vi.fn(),
            animationStatus: vi.fn()
        }
        simulation.player.set(player)
        flushSync()
        const clip = byLabel<HTMLSelectElement>('Clip')!
        clip.value = 'wave'
        clip.dispatchEvent(new Event('change', { bubbles: true }))
        button('Play')!.click()
        await vi.waitFor(() => expect(player.playClip).toHaveBeenCalledOnce())
        expect(player.playClip.mock.calls[0][0]).toMatchObject({ name: 'wave' })

        simulation.animationStatus.set(
            AnimationStatus.create({ name: 'wave', state: AnimationState.ANIM_PLAYING })
        )
        flushSync()
        expect(text()).toMatch('Playing wave')
        button('Stop')!.click()
        await vi.waitFor(() => expect(player.stopClip).toHaveBeenCalledOnce())
    })

    it("shows the robot's clip playing while the controls drive the robot", () => {
        const states = fakeStates()
        open(ModesEnum.ANIMATE)
        states.set(
            AnimationStatus,
            AnimationStatus.create({ name: 'sit', state: AnimationState.ANIM_HOLD })
        )
        flushSync()
        expect(text()).toMatch('Playing sit')
        expect(button('Play')!.disabled).toBe(false)
    })

    it('names what each stick does in the mode of the robot they drive', () => {
        const labels = () =>
            ['#left', '#right'].map(
                zone => document.body.querySelector(`${zone} .joystick-label`)!.textContent
            )
        open(ModesEnum.STAND)
        expect(labels()).toEqual(['Shift', 'Tilt'])
        mode.set(ModeData.create({ mode: ModesEnum.WALK }))
        flushSync()
        expect(labels()).toEqual(['Move', 'Turn'])
        driveSimulation(true)
        simulation.mode.set(ModesEnum.STAND)
        flushSync()
        expect(labels()).toEqual(['Shift', 'Tilt'])
    })

    it('marks a preview without a robot and offers no Deactivate', () => {
        open(ModesEnum.DEACTIVATED, { linked: false })
        expect(button('Deactivate')).toBeUndefined()
        expect(text()).toMatch('Preview')
    })
})
