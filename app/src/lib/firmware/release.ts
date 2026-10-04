/** Where the web flasher publishes the latest release's images (flasher/generate_manifests.py). */
export const RELEASE_SITE = 'https://runeharlyk.github.io/SpotMicroESP32-Leika/flash/firmware/'

interface ReleaseIndex {
    tag: string | null
    version: string | null
    images: Record<string, string>
}

export interface Release {
    tag: string
    download: () => Promise<Uint8Array>
}

/** The latest release's app image for `env`; undefined when there is no release or it has no image for that env. */
export async function latestRelease(
    env: string,
    fetcher: typeof fetch = fetch
): Promise<Release | undefined> {
    const index: ReleaseIndex = await (await get(fetcher, 'ota.json')).json()
    const file = index.images[env]
    if (!index.tag || !file) return undefined
    return {
        tag: index.tag,
        download: async () => new Uint8Array(await (await get(fetcher, file)).arrayBuffer())
    }
}

async function get(fetcher: typeof fetch, path: string) {
    const response = await fetcher(RELEASE_SITE + path)
    if (!response.ok) throw new Error(`The release site answered ${response.status} for ${path}`)
    return response
}
