// Minimal stand-in for SvelteKit's $app/state; tests assign to `page` to simulate navigation.
const initial: { url: URL; data: Record<string, unknown> } = {
    url: new URL('http://localhost/'),
    data: {}
}

export const page = $state(initial)
