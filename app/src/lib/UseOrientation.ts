import { onMount } from "svelte";
import { writable, type Writable } from "svelte/store";

onMount(() => {
    window.addEventListener("", () => {
        screen.orientation.addEventListener("change", _handleOrientationChange);
    })
})

export type OrientationType = 'portrait-primary' | 'portrait-secondary' | 'landscape-primary' | 'landscape-secondary'

export const orientation:Writable<OrientationType> = writable('portrait-primary');

export const isPortrait = writable(true);

const _isPortrait = (orientation:OrientationType | undefined):boolean => {
    return orientation === "portrait-primary" || orientation ===  "portrait-secondary";
}

const _handleOrientationChange = () => {
    orientation.set(screen.orientation.type)
    isPortrait.set(_isPortrait(screen.orientation.type))
}