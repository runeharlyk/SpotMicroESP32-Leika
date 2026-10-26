import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { get } from 'svelte/store'
import Controls from '../../src/routes/controller/Controls.svelte'
import { input, mode, walkGait } from '../../src/lib/stores/model-store'
import { ModeData, ModesEnum, WalkGaitData, WalkGaits } from '../../src/lib/platform_shared/message'

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

describe('controller panel', () => {
    let component: ReturnType<typeof mount> | undefined

    const open = (robotMode: ModesEnum) => {
        mode.set(ModeData.create({ mode: robotMode }))
        component = mount(Controls, { target: document.body })
        flushSync()
    }

    beforeEach(() => {
        walkGait.set(WalkGaitData.create({ gait: WalkGaits.TROT }))
        input.update(data => ({ ...data, height: 0.7, speed: 0.5, s1: 0.5 }))
    })

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        document.body.innerHTML = ''
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

    it('asks for the picked mode and gait, and deactivates directly', () => {
        open(ModesEnum.STAND)
        const modeSelect = byLabel<HTMLSelectElement>('Mode')!
        modeSelect.value = String(ModesEnum.WALK)
        modeSelect.dispatchEvent(new Event('change', { bubbles: true }))
        flushSync()
        expect(get(mode).mode).toBe(ModesEnum.WALK)

        const gait = byLabel<HTMLSelectElement>('Gait')!
        gait.value = String(WalkGaits.CRAWL)
        gait.dispatchEvent(new Event('change', { bubbles: true }))
        expect(get(walkGait).gait).toBe(WalkGaits.CRAWL)

        button('Deactivate')!.click()
        flushSync()
        expect(get(mode).mode).toBe(ModesEnum.DEACTIVATED)
        expect(modeSelect.value).toBe(String(ModesEnum.DEACTIVATED))
    })

    it('moves the body height the slider is set to', () => {
        open(ModesEnum.STAND)
        const height = slider('Body height') as HTMLInputElement
        height.value = '0.25'
        height.dispatchEvent(new Event('input', { bubbles: true }))
        expect(get(input).height).toBe(0.25)
    })

    it('shows a mode the robot reports but nobody picks, such as a clip playing', () => {
        open(ModesEnum.ANIMATE)
        const modeSelect = byLabel<HTMLSelectElement>('Mode')!
        expect(modeSelect.value).toBe(String(ModesEnum.ANIMATE))
        expect(modeSelect.selectedOptions[0].disabled).toBe(true)
    })
})
