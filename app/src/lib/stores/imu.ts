import { IMUData } from '$lib/platform_shared/message'
import { createHistoryStore } from './history-store'

export const imu = createHistoryStore(IMUData, 100)
