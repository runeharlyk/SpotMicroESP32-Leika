import { describe, expect, it } from 'vitest'
import {
    RELEASE_SITE,
    isNewerVersion,
    latestRelease,
    newerRelease
} from '../../src/lib/firmware/release'

// Stands in for the Pages site: each path answers with its body, any other with 404.
function site(files: Record<string, string | Uint8Array<ArrayBuffer>>) {
    const asked: string[] = []
    const fetcher = async (url: string | URL | Request) => {
        const path = String(url).replace(RELEASE_SITE, '')
        asked.push(path)
        const body = files[path]
        return body === undefined ? new Response('', { status: 404 }) : new Response(body)
    }
    return { asked, fetcher: fetcher as typeof fetch }
}

const index = JSON.stringify({
    tag: 'v0.3.1',
    version: '0.3.1',
    images: { 'esp32-wroom-camera': 'esp32-wroom-camera.bin' }
})

describe('latestRelease', () => {
    it('downloads the image of the robot env from the latest release', async () => {
        const { asked, fetcher } = site({
            'ota.json': index,
            'esp32-wroom-camera.bin': new Uint8Array([0xe9, 1, 2])
        })

        const release = await latestRelease('esp32-wroom-camera', fetcher)

        expect(release?.tag).toBe('v0.3.1')
        expect(release && [...(await release.download())]).toEqual([0xe9, 1, 2])
        expect(asked).toEqual(['ota.json', 'esp32-wroom-camera.bin'])
    })

    it('has nothing for an env the release did not build', async () => {
        const { fetcher } = site({ 'ota.json': index })
        expect(await latestRelease('esp32dev', fetcher)).toBeUndefined()
    })

    it('has nothing while no release exists', async () => {
        const { fetcher } = site({
            'ota.json': JSON.stringify({ tag: null, version: null, images: {} })
        })
        expect(await latestRelease('esp32-wroom-camera', fetcher)).toBeUndefined()
    })

    it('says so when the release site cannot be reached', async () => {
        const { fetcher } = site({})
        await expect(latestRelease('esp32-wroom-camera', fetcher)).rejects.toThrow('404')
    })
})

describe('isNewerVersion', () => {
    it('compares dotted numbers, not text', () => {
        expect(isNewerVersion('0.10.0', '0.9.3')).toBe(true)
        expect(isNewerVersion('0.9.3', '0.10.0')).toBe(false)
        expect(isNewerVersion('1.0', '0.99.99')).toBe(true)
    })

    it('treats a missing part as zero and an equal version as not newer', () => {
        expect(isNewerVersion('0.3', '0.3.0')).toBe(false)
        expect(isNewerVersion('0.3.0.1', '0.3')).toBe(true)
        expect(isNewerVersion('0.3.1', '0.3.1')).toBe(false)
    })

    it('ignores a leading v and anything after the numbers', () => {
        expect(isNewerVersion('v0.3.1', '0.3.0')).toBe(true)
        expect(isNewerVersion('0.3.1', '0.3.1-dirty')).toBe(false)
    })

    it('never calls a version without numbers newer, in either place', () => {
        expect(isNewerVersion('nightly', '0.3.0')).toBe(false)
        expect(isNewerVersion('0.4.0', 'fixture')).toBe(false)
    })
})

describe('newerRelease', () => {
    it('offers the latest release to a robot running an older version', async () => {
        const { asked, fetcher } = site({ 'ota.json': index })
        const release = await newerRelease('esp32-wroom-camera', '0.3.0', fetcher)
        expect(release?.tag).toBe('v0.3.1')
        expect(asked).toEqual(['ota.json'])
    })

    it('offers nothing to a robot already running it', async () => {
        const { fetcher } = site({ 'ota.json': index })
        expect(await newerRelease('esp32-wroom-camera', '0.3.1', fetcher)).toBeUndefined()
    })

    it('offers nothing when the release has no image for the robot env', async () => {
        const { fetcher } = site({ 'ota.json': index })
        expect(await newerRelease('esp32dev', '0.1.0', fetcher)).toBeUndefined()
    })
})
