import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import Firmware from '../../src/routes/system/firmware/Firmware.svelte'
import { connectionFeatures } from '../../src/lib/stores/featureFlags'
import { mode } from '../../src/lib/stores/model-store'
import { FeaturesDataResponse, ModeData, ModesEnum } from '../../src/lib/platform_shared/message'

const head = readFileSync(path.join(__dirname, 'fixtures', 'firmware-head.bin'))

const button = (label: RegExp) =>
    [...document.body.querySelectorAll('button')].find(b => label.test(b.textContent ?? ''))!

async function pick(env: string) {
    const input = document.body.querySelector<HTMLInputElement>('input[type=file]')!
    const bytes = new Uint8Array([...head, ...new TextEncoder().encode(`\0LEIKA_ENV=${env}\0`)])
    const file = new File([bytes], 'firmware.bin')
    // jsdom's File lacks the arrayBuffer() every browser has.
    Object.defineProperty(file, 'arrayBuffer', { value: async () => bytes.buffer })
    Object.defineProperty(input, 'files', { value: [file], configurable: true })
    input.dispatchEvent(new Event('change', { bubbles: true }))
    await vi.waitFor(() => expect(document.body.textContent).toMatch('v0.3.0-4-g8f158dd-dirty'))
}

describe('Firmware page', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        document.body.innerHTML = ''
    })

    function open(robotMode: ModesEnum) {
        connectionFeatures.set(
            FeaturesDataResponse.create({
                firmwareVersion: '0.3.0',
                firmwareBuiltTarget: 'esp32-wroom-camera'
            })
        )
        mode.set(ModeData.create({ mode: robotMode }))
        component = mount(Firmware, { target: document.body })
        flushSync()
    }

    it('offers the update of an image for this robot once it is deactivated', async () => {
        open(ModesEnum.STAND)
        await pick('esp32-wroom-camera')

        expect(document.body.textContent).not.toMatch('not for this robot')
        expect(button(/^Update$/).disabled).toBe(true)

        button(/^Deactivate$/).click()
        flushSync()
        expect(button(/^Update$/).disabled).toBe(false)
    })

    it('warns before updating with an image for another board', async () => {
        open(ModesEnum.DEACTIVATED)
        await pick('seeed-xiao-esp32s3')

        expect(document.body.textContent).toMatch(
            "This image is for seeed-xiao-esp32s3, not for this robot's esp32-wroom-camera"
        )
        expect(button(/Update anyway/).disabled).toBe(false)
    })
})
