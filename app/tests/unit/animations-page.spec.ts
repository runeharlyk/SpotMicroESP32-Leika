import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Animations from '../../src/routes/animations/Animations.svelte'
import { socket } from '../../src/lib/stores/socket'
import { mode } from '../../src/lib/stores/model-store'
import { fileSystemClient } from '../../src/lib/filesystem/chunkedTransfer'
import {
    AnimationReport,
    AnimationState,
    AnimationStatus,
    ModeData,
    ModesEnum
} from '../../src/lib/platform_shared/message'
import { ParamId } from '../../src/lib/platform_shared/animation'
import { fakeRobot } from './fake-robot'
import { robotClips } from '../../src/lib/animation/robot'

const CLIPS = [
    { name: 'bow', builtin: true, size: 167 },
    { name: 'nod', builtin: false, size: 90 }
]
const BOW = AnimationReport.create({
    ok: true,
    description: 'Lowers the front and comes back up',
    duration: 2.4,
    clampedMask: 0b000_000_111_000,
    params: [
        { id: ParamId.SPEED, min: 0.5, defaultValue: 1, max: 2 },
        { id: ParamId.REPEAT, min: 1, defaultValue: 2, max: 4 }
    ]
})
const NOD_JSON = JSON.stringify({ name: 'nod', schema: 1, keyframes: [{ time: 0 }, { time: 1 }] })

const button = (label: RegExp) =>
    [...document.body.querySelectorAll('button')].find(b => label.test(b.textContent ?? ''))!
const text = () => (document.body.textContent ?? '').replace(/\s+/g, ' ')

async function pick(name: string, content: string) {
    const input = document.body.querySelector<HTMLInputElement>('input[type=file]')!
    const file = new File([content], name)
    // jsdom's File lacks the text() every browser has.
    Object.defineProperty(file, 'text', { value: async () => content })
    Object.defineProperty(input, 'files', { value: [file], configurable: true })
    input.dispatchEvent(new Event('change', { bubbles: true }))
}

describe('Animations page', () => {
    let component: ReturnType<typeof mount> | undefined
    let robot: ReturnType<typeof fakeRobot>
    let played: unknown[]
    let reports: Record<string, AnimationReport>
    let statusListener: ((data: AnimationStatus) => void) | undefined

    beforeEach(() => {
        robotClips.set(null)
        played = []
        reports = { bow: BOW }
        robot = fakeRobot((name, data) => {
            if (name === 'animationListRequest') return { animationList: { animations: CLIPS } }
            if (name === 'animationValidate') {
                const report = reports[data.animationValidate!.name]
                return report ?
                        { animationReport: report }
                    :   {
                            statusCode: 422,
                            animationReport: AnimationReport.create({ error: 'no such clip' })
                        }
            }
            if (name === 'animationPlay') played.push(data.animationPlay)
            return {}
        })
        vi.spyOn(socket, 'on').mockImplementation(((type: unknown, listener: never) => {
            if (type === AnimationStatus) statusListener = listener
            return () => {}
        }) as typeof socket.on)
    })

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        document.body.innerHTML = ''
        vi.restoreAllMocks()
    })

    async function open(robotMode: ModesEnum) {
        mode.set(ModeData.create({ mode: robotMode }))
        component = mount(Animations, { target: document.body, props: { onEdit: () => {} } })
        await vi.waitFor(() => expect(text()).toMatch('nod'))
    }

    async function choose(name: string) {
        button(new RegExp(`^\\s*${name}\\s*$`)).click()
        await vi.waitFor(() =>
            expect(document.body.querySelector('[data-testid=clip-details]')).not.toBeNull()
        )
    }

    it('plays the chosen clip with the values on its sliders', async () => {
        await open(ModesEnum.STAND)
        await choose('bow')

        expect(text()).toMatch('Lowers the front and comes back up')
        expect(text()).toMatch('Repeats: 2 times')
        const speed = document.body.querySelector<HTMLInputElement>('input[type=range]')!
        speed.value = '1.5'
        speed.dispatchEvent(new Event('input', { bubbles: true }))
        flushSync()

        button(/Play/).click()
        await vi.waitFor(() => expect(played).toHaveLength(1))
        expect(played[0]).toEqual({
            name: 'bow',
            params: [
                { id: ParamId.SPEED, value: 1.5 },
                { id: ParamId.REPEAT, value: 2 }
            ]
        })
    })

    it('names the feet the clip cannot reach', async () => {
        await open(ModesEnum.STAND)
        await choose('bow')
        expect(text()).toMatch('The front right foot cannot reach')
    })

    it('asks to stand up before playing on a deactivated robot', async () => {
        await open(ModesEnum.DEACTIVATED)
        await choose('bow')
        expect(button(/Play/).disabled).toBe(true)

        button(/Stand up/).click()
        flushSync()
        expect(button(/Play/).disabled).toBe(false)
    })

    it('follows the robot through a clip and offers stop only while one runs', async () => {
        await open(ModesEnum.STAND)
        expect(button(/Stop/).disabled).toBe(true)

        statusListener!(
            AnimationStatus.create({ name: 'sit', state: AnimationState.ANIM_HOLD, t: 1.22 })
        )
        flushSync()
        expect(text()).toMatch('Holding sit at 1.2 s, until stopped')
        button(/Stop/).click()
        await vi.waitFor(() => expect(robot.sent).toContain('animationStop'))

        statusListener!(AnimationStatus.create({ name: 'sit', state: AnimationState.ANIM_IDLE }))
        flushSync()
        expect(text()).toMatch('No clip is playing.')
        expect(button(/Stop/).disabled).toBe(true)
    })

    it('uploads a valid clip and shows the robot report for it', async () => {
        await open(ModesEnum.STAND)
        const upload = vi.spyOn(fileSystemClient, 'uploadFile').mockResolvedValue({ success: true })
        reports.nod = AnimationReport.create({ ok: true, duration: 1 })

        await pick('nod.json', NOD_JSON)
        await vi.waitFor(() => expect(upload).toHaveBeenCalledOnce())
        expect(upload.mock.calls[0][0]).toBe('/animations/nod.pb')
        await vi.waitFor(() => expect(text()).toMatch('1.0 s'))
    })

    it('refuses a broken file and a clip named like a built-in without sending anything', async () => {
        await open(ModesEnum.STAND)
        const upload = vi.spyOn(fileSystemClient, 'uploadFile')

        await pick(
            'late.json',
            JSON.stringify({ name: 'late', schema: 1, keyframes: [{ time: 1 }] })
        )
        await vi.waitFor(() => expect(text()).toMatch('first keyframe must be at time 0'))
        await pick('bow.json', NOD_JSON.replace('"nod"', '"bow"'))
        await vi.waitFor(() => expect(text()).toMatch('bow is built into the firmware'))
        expect(upload).not.toHaveBeenCalled()
    })

    it('removes an upload the robot refuses', async () => {
        await open(ModesEnum.STAND)
        vi.spyOn(fileSystemClient, 'uploadFile').mockResolvedValue({ success: true })
        const remove = vi.spyOn(fileSystemClient, 'deleteFile').mockResolvedValue({ success: true })

        await pick('nod.json', NOD_JSON)
        await vi.waitFor(() => expect(text()).toMatch('The robot refused nod: no such clip'))
        expect(remove).toHaveBeenCalledWith('/animations/nod.pb')
        expect(document.body.querySelector('[data-testid=clip-details]')).toBeNull()
    })
})
