<script lang="ts">
  import { socket } from '$lib/stores'
  import { MessageTopic } from '$lib/types/models'
  import { throttler as Throttler } from '$lib/utilities'

  let { servoId = $bindable(0), pwm = $bindable(306) } = $props()

  let active = $state(false)

  let allServos = $state(false)

  const throttler = new Throttler()

  const activateServo = () => {
    socket.sendEvent(MessageTopic.servoState, { active: 1 })
  }

  const deactivateServo = () => {
    socket.sendEvent(MessageTopic.servoState, { active: 0 })
  }

  const updatePWM = () => {
    throttler.throttle(() => {
      socket.sendEvent(MessageTopic.servoPWM, { servo_id: servoId, pwm })
    }, 10)
  }

  const toggleMode = () => {
    servoId = allServos ? -1 : 0
  }
</script>

<div class="flex flex-col">
  <h2 class="text-lg">General servo configuration</h2>
  <span>Servo</span>
  <span>{pwm}</span>
</div>
<input
  type="range"
  min="80"
  max="600"
  bind:value={pwm}
  oninput={updatePWM}
  class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700" />

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
