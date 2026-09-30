<script lang="ts">
    import RequiresRobot from '$lib/components/RequiresRobot.svelte'
    import Servos from './servos.svelte'
    import ServoTable from './ServoTable.svelte'

    let servoId = $state(0)
    let pwm = $state(306)
</script>

<div class="mx-0 my-1 flex flex-col space-y-4 sm:mx-8 sm:my-8">
    <Servos bind:servoId bind:pwm />
    <RequiresRobot>
        <ServoTable {servoId} {pwm} />
    </RequiresRobot>
</div>
