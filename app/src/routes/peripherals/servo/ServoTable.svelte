<script lang="ts">
    import { robotRequest } from '$lib/robot-request'
    import { RotateCw, RotateCcw } from '$lib/components/icons'
    import type { ServoSettings } from '$lib/platform_shared/api'
    import { notifications } from '$lib/components/toasts/notifications'
    import Spinner from '$lib/components/Spinner.svelte'
    import LoadError from '$lib/components/LoadError.svelte'
    import LegDiagram from './LegDiagram.svelte'

    interface Props {
        servoSettings?: ServoSettings | null
        servoId?: number
        pwm?: number
    }

    let {
        servoSettings = $bindable(null),
        pwm = $bindable(306),
        servoId = $bindable(0)
    }: Props = $props()

    const syncConfig = async () => {
        if (!servoSettings) return
        notifications.info('Uploading servo config...', 3000)
        try {
            await robotRequest({ servoSettings })
            notifications.success('Servo config uploaded successfully', 3000)
        } catch (error) {
            notifications.error(`Servo config upload failed: ${(error as Error).message}`, 5000)
        }
    }

    const toggleDirection = async (index: number) => {
        if (!servoSettings) return
        servoSettings.servos[index].direction = servoSettings.servos[index].direction === 1 ? -1 : 1
        await syncConfig()
    }

    const getServoConfig = async () => {
        const reply = await robotRequest({ servoSettingsRequest: {} })
        if (!reply.servoSettings) throw new Error('The robot sent no servo config')
        servoSettings = reply.servoSettings
    }

    let loading = $state(getServoConfig())

    const setCenterPWM = async () => {
        if (!servoSettings) return
        servoSettings.servos[servoId].centerPwm = pwm
        await syncConfig()
    }
</script>

<div>
    <button class="btn btn-sm btn-primary" onclick={() => setCenterPWM()}>Set center pwm</button>
</div>

{#await loading}
    <Spinner />
{:catch error}
    <LoadError {error} retry={() => (loading = getServoConfig())} />
{/await}

{#if servoSettings}
    <LegDiagram servos={servoSettings.servos} {servoId} {pwm} />
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
                {#each servoSettings.servos as servo, index (index)}
                    <tr class="hover:bg-base-200">
                        <td class="font-medium">Servo {index}</td>
                        <td>
                            <input
                                type="number"
                                class="input input-sm input-bordered w-20"
                                value={servo.centerPwm}
                                onblur={syncConfig}
                                oninput={event =>
                                    (servo.centerPwm = Number(
                                        (event.target as HTMLInputElement).value
                                    ))}
                                min="80"
                                max="600"
                            />
                        </td>
                        <td>
                            <input
                                type="number"
                                step="0.1"
                                class="input input-sm input-bordered w-20"
                                value={servo.centerAngle}
                                onblur={syncConfig}
                                oninput={event =>
                                    (servo.centerAngle = Number(
                                        (event.target as HTMLInputElement).value
                                    ))}
                                min="-90"
                                max="90"
                            />
                        </td>
                        <td>
                            <button
                                class="btn btn-sm btn-ghost"
                                title="Toggle direction {servo.direction}"
                                onclick={() => toggleDirection(index)}
                            >
                                {#if servo.direction === 1}
                                    <RotateCw class="w-4 h-4 text-success" />
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
                                oninput={event =>
                                    (servo.conversion = Number(
                                        (event.target as HTMLInputElement).value
                                    ))}
                                min="0"
                                max="10"
                            />
                        </td>
                    </tr>
                {/each}
            </tbody>
        </table>
    </div>
{/if}
