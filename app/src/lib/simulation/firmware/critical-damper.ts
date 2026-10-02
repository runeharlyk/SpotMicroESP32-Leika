/** CriticalDamper (utils/critical_damper.h). */
export class CriticalDamper {
    velocity = 0

    /** From rest, (1 + w t) e^(-w t) of a step remains after t: 5% at w t = 4.744. */
    static omegaFor(settleSeconds: number) {
        return 4.744 / settleSeconds
    }

    step(value: number, target: number, dt: number, omega: number) {
        const offset = value - target
        const drive = this.velocity + omega * offset
        const decay = Math.exp(-omega * dt)
        this.velocity = (this.velocity - omega * drive * dt) * decay
        return target + (offset + drive * dt) * decay
    }
}
