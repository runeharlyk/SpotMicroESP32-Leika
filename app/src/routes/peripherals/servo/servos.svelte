<script lang="ts">
    import type { ServoSettings } from '$lib/platform_shared/api'
    import { ServoPWMData, ServoStateData } from '$lib/platform_shared/message'
    import { JOINT_NAMES, LEG_NAMES, jointChannel, servoJoint } from '$lib/calibration/leg-pose'
    import { socket } from '$lib/stores'
    import { Throttler } from '$lib/utilities'

    interface Props {
        jointId?: number
        pwm?: number
        servoSettings: ServoSettings | null
    }

    let { jointId = $bindable(0), pwm = $bindable(306), servoSettings }: Props = $props()

    let active = $state(false)

    let allServos = $state(false)

    const throttler = new Throttler()

    const activateServo = () => {
        socket.emit(ServoStateData, ServoStateData.create({ active: true }))
    }

    const deactivateServo = () => {
        socket.emit(ServoStateData, ServoStateData.create({ active: false }))
    }

    // The robot takes a channel, not a joint: until its channel map is loaded, a PWM could drive the wrong servo.
    const updatePWM = () => {
        if (!servoSettings) return
        const servoId = jointId === -1 ? -1 : jointChannel(servoSettings, jointId)
        throttler.throttle(() => {
            socket.emit(ServoPWMData, ServoPWMData.create({ servoId, servoPwm: pwm }))
        }, 10)
    }

    const toggleMode = () => {
        jointId = allServos ? -1 : 0
    }

    const selected = $derived(jointId === -1 ? null : servoJoint(jointId))
</script>

<div class="flex flex-col gap-6 p-4 bg-base-200 rounded-xl">
    <div class="flex flex-col gap-2">
        <h2 class="text-lg font-semibold">PWM Control</h2>
        <div class="flex items-center justify-between">
            <span class="text-sm opacity-70">PWM Value</span>
            <span class="text-2xl font-mono font-bold text-primary">{pwm}</span>
        </div>
        <input
            type="range"
            min="80"
            max="600"
            bind:value={pwm}
            oninput={updatePWM}
            class="range range-primary"
            disabled={!servoSettings}
        />
    </div>

    <div class="divider my-0"></div>

    <div class="flex flex-col gap-3">
        <h2 class="text-lg font-semibold">Joint Selection</h2>
        <label class="flex items-center justify-between cursor-pointer">
            <span>All joints</span>
            <input
                type="checkbox"
                class="toggle toggle-primary"
                bind:checked={allServos}
                onchange={toggleMode}
            />
        </label>
        <label class="flex items-center justify-between cursor-pointer">
            <span>Active</span>
            <input
                type="checkbox"
                class="toggle toggle-success"
                bind:checked={active}
                onchange={active ? activateServo : deactivateServo}
            />
        </label>
        <label class="flex items-center justify-between">
            <span>
                {#if selected}
                    {LEG_NAMES[selected.leg]} {JOINT_NAMES[selected.joint].toLowerCase()}
                {:else}
                    Every joint
                {/if}
            </span>
            <input
                type="range"
                min="0"
                max="11"
                step="1"
                bind:value={jointId}
                class="range range-sm w-32"
                disabled={allServos}
            />
        </label>
    </div>
</div>
