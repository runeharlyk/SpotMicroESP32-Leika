<script lang="ts">
  import { api } from '$lib/api'
  import { onMount } from 'svelte'
  import { RotateCw, RotateCcw } from '$lib/components/icons'
  interface Props {
    data?: any
    servoId?: number
    pwm?: number
  }

  let {
    data = $bindable({
      servos: []
    }),
    pwm = $bindable(306),
    servoId = $bindable(0)
  }: Props = $props()

  const updateValue = (event: Event, index: number, key: string) => {
    data.servos[index][key] = Number((event.target as HTMLInputElement).value)
  }

  const syncConfig = async () => {
    await api.post('/api/servo/config', data)
  }

  const toggleDirection = async (index: number) => {
    data.servos[index].direction = data.servos[index].direction === 1 ? -1 : 1
    await syncConfig()
  }

  onMount(async () => {
    const result = await api.get('/api/servo/config')
    if (result.isOk()) {
      data = result.inner
    }
  })

  const setCenterPWM = async () => {
    console.log('setCenterPWM', servoId, pwm)
    data.servos[servoId]['center_pwm'] = pwm
    await syncConfig()
  }
</script>

<div>
  <button class="btn btn-sm btn-primary" onclick={() => setCenterPWM()}>Set center pwm</button>
</div>

<div class="overflow-x-auto">
  <table class="table table-xs">
    <thead>
      <tr>
        <th>Servo</th>
        <th>Center PWM</th>
        <th>Center Angle</th>
        <th>Direction</th>
        <th>Conversion</th>
      </tr>
    </thead>
    <tbody>
      {#each data.servos as servo, index}
        <tr class="hover:bg-base-200">
          <td class="font-medium">Servo {index}</td>
          <td>
            <input
              type="number"
              class="input input-sm input-bordered w-20"
              value={servo.center_pwm}
              onblur={syncConfig}
              oninput={event => updateValue(event, index, 'center_pwm')}
              min="80"
              max="600" />
          </td>
          <td>
            <input
              type="number"
              step="0.1"
              class="input input-sm input-bordered w-20"
              value={servo.center_angle}
              onblur={syncConfig}
              oninput={event => updateValue(event, index, 'center_angle')}
              min="-90"
              max="90" />
          </td>
          <td>
            <button
              class="btn btn-sm btn-ghost"
              title="Toggle direction {servo.direction}"
              onclick={() => toggleDirection(index)}>
              {#if servo.direction === 1}
                <RotateCw class="w-4 h-4 text-green-500" />
              {:else}
                <RotateCcw class="w-4 h-4" />
              {/if}
            </button>
          </td>
          <td>
            <input
              type="number"
              step="0.01"
              class="input input-sm input-bordered w-20"
              value={servo.conversion}
              onblur={syncConfig}
              oninput={event => updateValue(event, index, 'conversion')}
              min="0"
              max="10" />
          </td>
        </tr>
      {/each}
    </tbody>
  </table>
</div>
