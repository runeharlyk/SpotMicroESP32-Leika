<script lang="ts">
    import RequiresRobot from '$lib/components/RequiresRobot.svelte'
    import type { ServoSettings } from '$lib/platform_shared/api'
    import Servos from './servos.svelte'
    import ServoTable from './ServoTable.svelte'

    let jointId = $state(0)
    let pwm = $state(306)
    let servoSettings: ServoSettings | null = $state(null)
</script>

<div class="mx-0 my-1 flex flex-col space-y-4 sm:mx-8 sm:my-8">
    <Servos bind:jointId bind:pwm {servoSettings} />
    <RequiresRobot>
        <ServoTable bind:servoSettings {jointId} {pwm} />
    </RequiresRobot>
</div>
