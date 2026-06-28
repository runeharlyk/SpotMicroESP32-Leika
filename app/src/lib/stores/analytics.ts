import { AnalyticsData } from '$lib/platform_shared/message'
import { createHistoryStore } from './history-store'

export const analytics = createHistoryStore(AnalyticsData, 100)
