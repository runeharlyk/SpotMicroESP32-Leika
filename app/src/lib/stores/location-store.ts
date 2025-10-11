import { persistentStore } from '$lib/utilities'
import { writable } from 'svelte/store'
import { PUBLIC_VITE_USE_HOST_NAME } from '$env/static/public'

export const location = PUBLIC_VITE_USE_HOST_NAME ? writable('') : persistentStore('location', '')
