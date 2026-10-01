<script lang="ts">
    import type { Servo } from '$lib/platform_shared/api'
    import { reportedVariant } from '$lib/stores/featureFlags'
    import { kinConfig } from '$lib/simulation/firmware/kin-config'
    import {
        JOINT_NAMES,
        LEG_NAMES,
        calibrationPose,
        legPoints,
        servoAngleFromPwm,
        servoJoint,
        type LegPoint
    } from '$lib/calibration/leg-pose'

    interface Props {
        servos: Servo[]
        servoId: number
        pwm: number
    }

    let { servos, servoId, pwm }: Props = $props()

    // The faint leg shows where a slightly higher PWM puts it: the way the real leg must move as the slider rises.
    const PWM_STEP = 20

    const cfg = $derived(kinConfig($reportedVariant ?? 'SPOTMICRO_ESP32'))
    const pose = $derived(calibrationPose(cfg, servos, servoId, pwm))
    const ahead = $derived(calibrationPose(cfg, servos, servoId, pwm + PWM_STEP))
    const reach = $derived(cfg.coxa + cfg.femur + cfg.tibia)
    const viewBox = $derived(`${-reach} ${-0.3 * reach} ${2 * reach} ${1.4 * reach}`)

    // Seen from behind, so a right leg reaches out to the right; legIk's x points towards the body.
    const front = (leg: number, [x, y]: LegPoint) => [leg % 2 === 0 ? -x : x, -y]
    // Seen from the robot's right, forward to the right.
    const side = ([, y, z]: LegPoint) => [z, -y]

    const polyline = (points: number[][]) => points.map(([x, y]) => `${x},${y}`).join(' ')

    const legView = (leg: number, angles: number[]) => {
        const { hipJoint, coxaEnd, kneeJoint, foot } = legPoints(
            cfg,
            angles.slice(leg * 3, leg * 3 + 3)
        )
        return {
            front: polyline([hipJoint, coxaEnd, kneeJoint, foot].map(point => front(leg, point))),
            side: polyline([coxaEnd, kneeJoint, foot].map(side)),
            femur: polyline([coxaEnd, kneeJoint].map(side)),
            tibia: polyline([kneeJoint, foot].map(side))
        }
    }

    const selected = $derived(servoId === -1 ? null : servoJoint(servoId))
    const moves = (leg: number) => servoId === -1 || selected?.leg === leg

    // Laid out as seen from above with the front at the top: left legs on the left.
    const LAYOUT = [1, 0, 3, 2]
</script>

<div class="flex flex-col gap-3 p-4 bg-base-200 rounded-xl">
    <div class="flex flex-wrap items-baseline justify-between gap-2">
        <h2 class="text-lg font-semibold">Expected pose</h2>
        {#if selected}
            <span class="text-sm">
                {LEG_NAMES[selected.leg]}
                {JOINT_NAMES[selected.joint].toLowerCase()}:
                <span class="font-mono font-bold text-primary"
                    >{servoAngleFromPwm(servos[servoId], pwm).toFixed(1)}°</span
                >
            </span>
        {:else}
            <span class="text-sm opacity-70">All servos follow the PWM</span>
        {/if}
    </div>
    <p class="text-xs opacity-70">
        Solid: where the calibration puts the joint at this PWM. Faint: {PWM_STEP} PWM higher. A real
        leg that moves the other way as the PWM rises needs its direction flipped; one that sits elsewhere
        at the centre PWM needs its centre adjusted.
    </p>
    <div class="grid grid-cols-2 gap-3">
        {#each LAYOUT as leg (leg)}
            {@const now = legView(leg, pose)}
            {@const next = legView(leg, ahead)}
            <div class="flex flex-col gap-1 rounded-lg p-2 {moves(leg) ? 'bg-base-300' : ''}">
                <span class="text-xs font-medium">{LEG_NAMES[leg]}</span>
                <div class="grid grid-cols-2 gap-1">
                    <figure>
                        <svg
                            {viewBox}
                            class="w-full text-base-content"
                            role="img"
                            aria-label="{LEG_NAMES[leg]} leg from behind"
                        >
                            <line
                                x1={-reach}
                                y1="0"
                                x2={reach}
                                y2="0"
                                class="stroke-current opacity-20"
                                stroke-width={reach / 80}
                            />
                            {#if moves(leg)}
                                <polyline
                                    points={next.front}
                                    fill="none"
                                    class="stroke-primary opacity-50"
                                    stroke-width={reach / 30}
                                    stroke-dasharray="{reach / 25} {reach / 40}"
                                />
                            {/if}
                            <polyline
                                points={now.front}
                                fill="none"
                                class={selected?.leg === leg && selected.joint === 0 ?
                                    'stroke-primary'
                                :   'stroke-current opacity-70'}
                                stroke-width={reach / 25}
                                stroke-linejoin="round"
                                stroke-linecap="round"
                            />
                        </svg>
                        <figcaption class="text-center text-[10px] opacity-60">
                            From behind
                        </figcaption>
                    </figure>
                    <figure>
                        <svg
                            {viewBox}
                            class="w-full text-base-content"
                            role="img"
                            aria-label="{LEG_NAMES[leg]} leg from the right"
                        >
                            <line
                                x1={-reach}
                                y1="0"
                                x2={reach}
                                y2="0"
                                class="stroke-current opacity-20"
                                stroke-width={reach / 80}
                            />
                            {#if moves(leg)}
                                <polyline
                                    points={next.side}
                                    fill="none"
                                    class="stroke-primary opacity-50"
                                    stroke-width={reach / 30}
                                    stroke-dasharray="{reach / 25} {reach / 40}"
                                />
                            {/if}
                            <polyline
                                points={now.femur}
                                fill="none"
                                class={selected?.leg === leg && selected.joint === 1 ?
                                    'stroke-primary'
                                :   'stroke-current opacity-70'}
                                stroke-width={reach / 25}
                                stroke-linecap="round"
                            />
                            <polyline
                                points={now.tibia}
                                fill="none"
                                class={selected?.leg === leg && selected.joint === 2 ?
                                    'stroke-primary'
                                :   'stroke-current opacity-70'}
                                stroke-width={reach / 25}
                                stroke-linecap="round"
                            />
                        </svg>
                        <figcaption class="text-center text-[10px] opacity-60">
                            From the right, front →
                        </figcaption>
                    </figure>
                </div>
            </div>
        {/each}
    </div>
</div>
