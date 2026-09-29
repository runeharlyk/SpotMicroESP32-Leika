// jsdom lacks the Web Animations API that Svelte transitions drive, so outros would never finish
// and removed elements would linger. This stand-in completes every animation on the next tick.
Element.prototype.animate = function () {
    const animation = {
        onfinish: null as (() => void) | null,
        cancel: () => {},
        finished: Promise.resolve()
    }
    setTimeout(() => animation.onfinish?.())
    return animation as unknown as Animation
}
