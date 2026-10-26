// The controller page, not the simulation view, decides which robot the controls drive: the simulation's while the
// chosen layout shows a Simulation widget anywhere, the real robot's otherwise and once the page is left.
import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { get } from 'svelte/store'
import { drivesSimulation } from '../../src/lib/stores/drive'
import { offlineView, selectedView } from '../../src/lib/stores/application'

vi.mock('$lib/stores/widget-components', async () => {
    const Stub = (await import('../stubs/AnimationPreviewStub.svelte')).default
    return {
        WidgetComponents: { Visualization: Stub, Stream: Stub, ChartWidget: Stub, Simulation: Stub }
    }
})

const show = (view: string) => {
    selectedView.set(view)
    offlineView.set(view)
}

describe('controller page', () => {
    let component: ReturnType<typeof mount> | undefined

    const open = async () => {
        const { default: Page } = await import('../../src/routes/controller/+page.svelte')
        component = mount(Page, { target: document.body })
        flushSync()
    }

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        document.body.innerHTML = ''
    })

    it('hands the controls to the simulation while the layout shows it, and back when it does not', async () => {
        show('3D representation')
        await open()
        expect(get(drivesSimulation)).toBe(false)

        show('Robot and simulation')
        flushSync()
        expect(get(drivesSimulation)).toBe(true)

        show('Stream')
        flushSync()
        expect(get(drivesSimulation)).toBe(false)
    })

    it('gives the controls back to the robot when the page is left', async () => {
        show('Simulation')
        await open()
        expect(get(drivesSimulation)).toBe(true)
        unmount(component!)
        component = undefined
        expect(get(drivesSimulation)).toBe(false)
    })
})
