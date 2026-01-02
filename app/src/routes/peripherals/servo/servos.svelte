<script lang="ts">
    import { ServoPWMData, ServoStateData } from '$lib/platform_shared/websocket_message'
    import { socket } from '$lib/stores'
    import { Throttler } from '$lib/utilities'

    let { servoId = $bindable(0), pwm = $bindable(306) } = $props()

    let active = $state(false)

    let allServos = $state(false)

    const throttler = new Throttler()

    const activateServo = () => {
        socket.sendEvent(ServoStateData, ServoStateData.create({active: true}))
    }

    const deactivateServo = () => {
        socket.sendEvent(ServoStateData, ServoStateData.create({active: false}))
    }

    const updatePWM = () => {
        throttler.throttle(() => {
            socket.sendEvent(ServoPWMData,  ServoPWMData.create({ servoId: servoId, servoPwm: pwm }))
        }, 10)
    }

    const toggleMode = () => {
        servoId = allServos ? -1 : 0
    }
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
        />
    </div>

    <div class="divider my-0"></div>

    <div class="flex flex-col gap-3">
        <h2 class="text-lg font-semibold">Servo Selection</h2>
        <label class="flex items-center justify-between cursor-pointer">
            <span>All servos</span>
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
            <span>Servo {servoId}</span>
            <input
                type="range"
                min="0"
                max="11"
                step="1"
                bind:value={servoId}
                class="range range-sm w-32"
                disabled={allServos}
            />
        </label>
    </div>
</div>
