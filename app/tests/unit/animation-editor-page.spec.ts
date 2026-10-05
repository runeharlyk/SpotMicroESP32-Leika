import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { modals } from 'svelte-modals'
import { page } from '../stubs/app-state.svelte'
import { fakeRobot } from './fake-robot'
import { fileSystemClient } from '../../src/lib/filesystem/chunkedTransfer'
import { robotClips } from '../../src/lib/animation/robot'
import { Animation } from '../../src/lib/platform_shared/animation'
import { saveFile } from '../../src/lib/utilities/save-file'

vi.mock('../../src/lib/components/AnimationPreview.svelte', async () => ({
    default: (await import('../stubs/AnimationPreviewStub.svelte')).default
}))
vi.mock('../../src/lib/utilities/save-file', () => ({ saveFile: vi.fn() }))

const { default: AnimationsPage } = await import('../../src/routes/animations/+page.svelte')

const CLIPS = [
    { name: 'bow', builtin: true, size: 167 },
    { name: 'wave', builtin: true, size: 302 },
    { name: 'nod', builtin: false, size: 40 }
]
const NOD = Animation.fromPartial({
    name: 'nod',
    schema: 1,
    keyframes: [{ time: 0 }, { time: 1, body: { pitch: 0.2 } }]
})

const button = (label: RegExp) =>
    [...document.body.querySelectorAll('button')].find(b =>
        label.test(b.getAttribute('aria-label') ?? b.textContent ?? '')
    )!
const text = () => (document.body.textContent ?? '').replace(/\s+/g, ' ')
const handles = () =>
    document.body.querySelector('[data-testid=preview]')?.getAttribute('data-handles')

async function edit(name: string) {
    button(new RegExp(`^Edit ${name}$`)).click()
    await vi.waitFor(() =>
        expect(document.body.querySelector('[data-testid=document-state]')?.textContent).toMatch(
            name
        )
    )
}

describe('animation editor page', () => {
    let component: ReturnType<typeof mount> | undefined

    beforeEach(async () => {
        robotClips.set(null)
        page.url = new URL('http://spot-micro.local/animations')
        fakeRobot(name =>
            name === 'animationListRequest' ? { animationList: { animations: CLIPS } } : {}
        )
        component = mount(AnimationsPage, { target: document.body })
        await vi.waitFor(() => expect(text()).toMatch('nod'))
    })

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        document.body.innerHTML = ''
        vi.restoreAllMocks()
        vi.mocked(saveFile).mockClear()
    })

    it('opens a built-in clip from the app with a handle on every foot', async () => {
        await edit('wave')
        expect(handles()).toBe('0,1,2,3')
        expect(text()).toMatch('Body at 0.00 s')
    })

    it('offers handles only for foot legs', async () => {
        await edit('wave')
        button(/^Keyframe at 1\.30 s$/).dispatchEvent(
            new PointerEvent('pointerdown', { bubbles: true })
        )
        flushSync()
        const target = document.body.querySelector<HTMLSelectElement>(
            'select[aria-label="Front left target"]'
        )!
        target.value = 'joints'
        target.dispatchEvent(new Event('change', { bubbles: true }))
        flushSync()
        expect(handles()).toBe('1,2,3')
    })

    it('opens an uploaded clip read back from the robot', async () => {
        const read = vi.spyOn(fileSystemClient, 'downloadFile').mockResolvedValue({
            success: true,
            data: Animation.encode(NOD).finish()
        })
        await edit('nod')
        expect(read).toHaveBeenCalledWith('/animations/nod.pb')
    })

    it('asks before discarding unsaved edits', async () => {
        const ask = vi.spyOn(modals, 'open').mockImplementation(() => Promise.resolve())
        await edit('wave')
        const roll = [...document.body.querySelectorAll('label')]
            .find(l => l.textContent?.includes('Roll (deg)'))!
            .querySelector('input')!
        roll.value = '5'
        roll.dispatchEvent(new Event('change', { bubbles: true }))
        flushSync()
        expect(text()).toMatch('wave, unsaved')

        button(/^Library$/).click()
        flushSync()
        button(/^Edit bow$/).click()
        flushSync()
        expect(ask).toHaveBeenCalledOnce()
        button(/^Editor$/).click()
        flushSync()
        expect(text()).toMatch('wave, unsaved')
    })

    it('refuses to save over a built-in', async () => {
        const upload = vi.spyOn(fileSystemClient, 'uploadFile')
        await edit('wave')
        button(/Save to robot/).click()
        await vi.waitFor(() =>
            expect(text()).toMatch('wave is built into the firmware; rename the clip to upload it')
        )
        expect(upload).not.toHaveBeenCalled()
    })

    it('downloads the JSON of the edited clip', async () => {
        await edit('wave')
        button(/Download JSON/).click()
        expect(saveFile).toHaveBeenCalledOnce()
        const [json, filename] = vi.mocked(saveFile).mock.calls[0]
        expect(filename).toBe('wave.json')
        expect(JSON.parse(json as string)).toMatchObject({ name: 'wave', schema: 1 })
    })
})
