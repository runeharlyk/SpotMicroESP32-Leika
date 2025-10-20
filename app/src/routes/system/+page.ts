import type { PageLoad } from './$types'
import { goto } from '$app/navigation'
import { resolve } from '$app/paths'

export const load = (async () => {
    goto(resolve('/'))
    return
}) satisfies PageLoad
