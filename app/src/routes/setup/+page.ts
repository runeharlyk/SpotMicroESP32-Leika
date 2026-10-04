import type { PageLoad } from './$types'

export const load = (async () => {
    return {
        title: 'Setup over USB'
    }
}) satisfies PageLoad
