// Minimal stand-in for SvelteKit's $app/paths so library modules can be unit tested outside kit.
export const base = ''
export const assets = ''
export const resolve = (path: string) => path
