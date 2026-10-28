import { PowerData } from '$lib/platform_shared/message'
import { createHistoryStore } from './history-store'

/** The servo rail's last ten seconds of readings, streamed while something listens. */
export const power = createHistoryStore(PowerData, 100)
