// jsdom lacks the Web Animations API that Svelte transitions drive, so outros would never finish
// and removed elements would linger. This stand-in completes every animation on the next tick.
// Node-environment tests (the MuJoCo physics) have no DOM to patch.
if (typeof Element !== 'undefined') {
    Element.prototype.animate = function () {
        const animation = {
            onfinish: null as (() => void) | null,
            cancel: () => {},
            finished: Promise.resolve()
        }
        setTimeout(() => animation.onfinish?.())
        return animation as unknown as Animation
    }
}
