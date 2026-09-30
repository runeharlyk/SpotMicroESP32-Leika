import { notifications } from '$lib/components/toasts/notifications'
import { robotRequest } from '$lib/robot-request'
import type { CorrelationRequest } from '$lib/platform_shared/message'

// The robot confirms before it goes down, so success means the command was accepted.
async function command(
    request: Omit<CorrelationRequest, 'correlationId'>,
    action: string,
    accepted: string
) {
    try {
        await robotRequest(request)
        notifications.success(accepted, 3000)
    } catch (error) {
        notifications.error(`${action} failed: ${(error as Error).message}`, 5000)
    }
}

export const restartRobot = () =>
    command({ systemRestart: {} }, 'Restart', 'The robot is restarting')

export const factoryResetRobot = () =>
    command({ systemReset: {} }, 'Factory reset', 'The robot is resetting to factory defaults')
