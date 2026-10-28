<script lang="ts">
    import type { JointModel } from '$lib/platform_shared/api'
    import { modelVariant } from '$lib/stores'
    import { JOINT_NAMES, servoJoint } from '$lib/calibration/leg-pose'
    import { calibrationJointValues } from '$lib/robot-pose'
    import { JOINT_MAPS } from '$lib/simulation/robots'
    import Visualization from '$lib/components/Visualization.svelte'

    interface Props {
        model: JointModel
        centers: number[]
        jointId: number
        pwm: number
    }

    let { model, centers, jointId, pwm }: Props = $props()

    // Every variant's joint map takes the firmware's legs 0 to 3 onto the model's front left, front right, rear left
    // and rear right legs (robots.ts); whether the model's left is the robot's left is the open "+z side" question.
    const MODEL_LEGS = ['front left', 'front right', 'rear left', 'rear right']

    const joints = $derived(calibrationJointValues($modelVariant, model, centers, jointId, pwm))
    const selected = $derived(jointId === -1 ? null : servoJoint(jointId))
    const highlight = $derived(
        jointId === -1 ? undefined : JOINT_MAPS[$modelVariant].joints[jointId]
    )
</script>

<div class="flex flex-col gap-3 p-4 bg-base-200 rounded-xl">
    <div class="flex flex-wrap items-baseline justify-between gap-2">
        <h2 class="text-lg font-semibold">3D model</h2>
        {#if selected}
            <span class="text-sm">
                Leg {selected.leg} on the model's {MODEL_LEGS[selected.leg]}
                {JOINT_NAMES[selected.joint].toLowerCase()}, highlighted
            </span>
        {/if}
    </div>
    <p class="text-xs opacity-70">
        The model at this PWM, through the robot's joint model; the other joints stand. Its legs 0
        to 3 are the model's front left, front right, rear left and rear right.
        {#if $modelVariant === 'SPOTMICRO_ESP32_MINI'}
            Each knee turns through its four-bar from where the joint is at its centre PWM, so it
            shows where the real tibia goes rather than where the firmware means it to.
        {/if}
    </p>
    <div class="relative h-72 w-full overflow-hidden rounded-lg sm:h-96">
        <Visualization {joints} {highlight} orbit={false} defaultColor={null} />
    </div>
</div>
