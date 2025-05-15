<script lang="ts">
  import SettingsCard from '$lib/components/SettingsCard.svelte'
  import type { ServoConfiguration, Servo } from '$lib/types/models'
  import Spinner from '$lib/components/Spinner.svelte'

  import { socket } from '$lib/stores'
  import { onDestroy, onMount } from 'svelte'
  import { throttler as Throttler } from '$lib/utilities'
  import { MotorOutline } from '$lib/components/icons'

  let isLoading = false

  let active = $state(false)

  let servoId = $state(0)

  let allServos = $state(false)

  const throttler = new Throttler()

  const sweep = (event: any) => {
    let channel = event.detail.channel
    socket.sendEvent('servoConfiguration', { servos: [{ channel, sweep: true }] })
  }

  const activateServo = (event: any) => {
    socket.sendEvent('servoState', { active: 1 })
  }

  const deactivateServo = (event: any) => {
    socket.sendEvent('servoState', { active: 0 })
  }

  let pwm = $state(306)

  const updatePWM = () => {
    throttler.throttle(() => {
      socket.sendEvent('servoPWM', { servo_id: servoId, pwm })
    }, 10)
  }

  const toggleMode = () => {
    servoId = allServos ? -1 : 0
  }
</script>

<SettingsCard collapsible={false}>
  {#snippet icon()}
    <MotorOutline class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
  {/snippet}
  {#snippet title()}
    <span>Servo</span>
  {/snippet}
  {pwm}
  <input
    type="range"
    min="80"
    max="600"
    bind:value={pwm}
    oninput={updatePWM}
    class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700" />

  {#if isLoading}
    <Spinner />
  {:else}
    <div class="flex flex-col">
      <h2 class="text-lg">General servo configuration</h2>
      <span>
        <label for="mode">All servoes</label>
        <input type="checkbox" class="toggle" bind:checked={allServos} onchange={toggleMode} />
      </span>
      <span>
        <label for="active">Active</label>
        <input
          type="checkbox"
          class="toggle"
          bind:checked={active}
          onchange={active ? activateServo : deactivateServo} />
      </span>
      <span class="flex items-center gap-2">
        <label for="servoId">Servo active {servoId}</label>
        <input type="range" min="0" max="11" step="1" bind:value={servoId} />
      </span>
    </div>
  {/if}
</SettingsCard>
