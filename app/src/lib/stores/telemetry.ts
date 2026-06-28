import { DownloadOTAData, RSSIData } from '$lib/platform_shared/message'
import { writable } from 'svelte/store'

type telemetry_data_type = {
    rssi: RSSIData
    download_ota: DownloadOTAData
    latency: number
}
const telemetry_data: telemetry_data_type = {
    rssi: RSSIData.create(),
    download_ota: DownloadOTAData.create(),
    latency: -1
}

function createTelemetry() {
    const { subscribe, update } = writable(telemetry_data)

    return {
        subscribe,
        setRSSI: (data: RSSIData) => {
            update(telemetry_data => {
                telemetry_data.rssi = data
                return telemetry_data
            })
        },
        setDownloadOTA: (data: DownloadOTAData) => {
            update(telemetry_data => {
                telemetry_data.download_ota = data
                return telemetry_data
            })
        },
        setLatency: (ms: number) => {
            update(telemetry_data => {
                telemetry_data.latency = ms
                return telemetry_data
            })
        }
    }
}

export const telemetry = createTelemetry()
