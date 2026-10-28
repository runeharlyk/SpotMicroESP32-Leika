/** Where the web flasher publishes the latest release's images (flasher/generate_manifests.py). */
export const RELEASE_SITE = 'https://runeharlyk.github.io/SpotMicroESP32-Leika/flash/firmware/'

interface ReleaseIndex {
    tag: string | null
    version: string | null
    images: Record<string, string>
}

export interface Release {
    tag: string
    version: string
    /** Where the app image lies, for a robot to download it itself. */
    url: string
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
        version: index.version ?? index.tag.replace(/^v/, ''),
        url: RELEASE_SITE + file,
        download: async () => new Uint8Array(await (await get(fetcher, file)).arrayBuffer())
    }
}

/**
 * Whether release `candidate` is newer than the `running` firmware, compared as dotted numbers with a leading v
 * ignored; a version that does not start with a number is never newer, so a development build is not nagged.
 */
export function isNewerVersion(candidate: string, running: string): boolean {
    const parse = (version: string) =>
        /^v?(\d+(?:\.\d+)*)/.exec(version)?.[1].split('.').map(Number)
    const a = parse(candidate)
    const b = parse(running)
    if (!a || !b) return false
    for (let i = 0; i < Math.max(a.length, b.length); i++) {
        const difference = (a[i] ?? 0) - (b[i] ?? 0)
        if (difference !== 0) return difference > 0
    }
    return false
}

/** The latest release when it is newer than the `running` version and has an image for `env`. */
export async function newerRelease(
    env: string,
    running: string,
    fetcher: typeof fetch = fetch
): Promise<Release | undefined> {
    const release = await latestRelease(env, fetcher)
    return release && isNewerVersion(release.version, running) ? release : undefined
}

async function get(fetcher: typeof fetch, path: string) {
    const response = await fetcher(RELEASE_SITE + path)
    if (!response.ok) throw new Error(`The release site answered ${response.status} for ${path}`)
    return response
}
