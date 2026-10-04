import { describe, expect, it } from 'vitest'
import { RELEASE_SITE, latestRelease } from '../../src/lib/firmware/release'

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
