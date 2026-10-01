<script lang="ts">
    import { robotRequest } from '$lib/robot-request'
    import type { ServoSettings } from '$lib/platform_shared/api'
    import { notifications } from '$lib/components/toasts/notifications'
    import Spinner from '$lib/components/Spinner.svelte'
    import LoadError from '$lib/components/LoadError.svelte'
    import {
        JOINT_NAMES,
        LEG_NAMES,
        channelsProblem,
        jointChannel,
        servoJoint
    } from '$lib/calibration/leg-pose'
    import LegDiagram from './LegDiagram.svelte'

    interface Props {
        servoSettings?: ServoSettings | null
        jointId?: number
        pwm?: number
    }

    let { servoSettings = $bindable(null), jointId = 0, pwm = 306 }: Props = $props()

    // The map as edited here; a robot without a stored map shows joint j on channel j, and keeps it until one is edited.
    let channels: number[] = $state([])
    let channelsEdited = $state(false)
    const channelError = $derived(channelsEdited ? channelsProblem(channels) : null)

    const syncConfig = async () => {
        if (!servoSettings || channelError) return
        const saved: ServoSettings = {
            servos: servoSettings.servos,
            channels: channelsEdited ? [...channels] : servoSettings.channels,
            model: undefined
        }
        notifications.info('Uploading servo config...', 3000)
        try {
            await robotRequest({ servoSettings: saved })
            servoSettings.channels = saved.channels
            notifications.success('Servo config uploaded successfully', 3000)
        } catch (error) {
            notifications.error(`Servo config upload failed: ${(error as Error).message}`, 5000)
        }
    }

    const getServoConfig = async () => {
        const reply = await robotRequest({ servoSettingsRequest: {} })
        const settings = reply.servoSettings
        if (!settings) throw new Error('The robot sent no servo config')
        servoSettings = settings
        channels = settings.servos.map((_, joint) => jointChannel(settings, joint))
        channelsEdited = false
    }

    let loading = $state(getServoConfig())

    const setCenterPWM = async () => {
        if (!servoSettings || jointId === -1) return
        servoSettings.servos[jointId].centerPwm = pwm
        await syncConfig()
    }

    const editChannel = (joint: number, value: number) => {
        channels[joint] = value
        channelsEdited = true
    }

    const jointName = (joint: number) => {
        const { leg, joint: part } = servoJoint(joint)
        return `${LEG_NAMES[leg]} ${JOINT_NAMES[part].toLowerCase()}`
    }
</script>

<div>
    <button class="btn btn-sm btn-primary" onclick={() => setCenterPWM()} disabled={jointId === -1}>
        Set centre PWM
    </button>
</div>

{#await loading}
    <Spinner />
{:catch error}
    <LoadError {error} retry={() => (loading = getServoConfig())} />
{/await}

{#if servoSettings}
    {#if servoSettings.model}
        <LegDiagram
            model={servoSettings.model}
            centers={servoSettings.servos.map(servo => servo.centerPwm)}
            {jointId}
            {pwm}
        />
    {:else}
        <div role="alert" class="alert alert-warning alert-soft">
            This robot's firmware predates the joint model; update it to calibrate here.
        </div>
    {/if}
    {#if channelError}
        <div role="alert" class="alert alert-error alert-soft">{channelError}; not saved.</div>
    {/if}
    <div class="overflow-x-auto">
        <table class="table table-xs">
            <thead>
                <tr>
                    <th>Joint</th>
                    <th>Channel</th>
                    <th>Centre PWM</th>
                </tr>
            </thead>
            <tbody>
                {#each servoSettings.servos as servo, joint (joint)}
                    <tr class="hover:bg-base-200 {joint === jointId ? 'bg-base-200' : ''}">
                        <td class="font-medium">{jointName(joint)}</td>
                        <td>
                            <input
                                type="number"
                                class="input input-sm input-bordered w-16"
                                value={channels[joint]}
                                onblur={syncConfig}
                                oninput={event =>
                                    editChannel(
                                        joint,
                                        Number((event.target as HTMLInputElement).value)
                                    )}
                                min="0"
                                max="15"
                            />
                        </td>
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
                                min="125"
                                max="600"
                            />
                        </td>
                    </tr>
                {/each}
            </tbody>
        </table>
    </div>
{/if}
