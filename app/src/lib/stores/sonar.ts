import { SonarData } from '$lib/platform_shared/message'
import { createHistoryStore } from './history-store'

/** The sonars' latest pings, streamed while something listens. */
export const sonar = createHistoryStore(SonarData, 1)
