import { DownloadOTAData, RSSIData } from '$lib/platform_shared/websocket_message'
import { writable } from 'svelte/store'

type telemetry_data_type = {
    rssi: RSSIData
    download_ota: DownloadOTAData
}
const telemetry_data: telemetry_data_type = {
    rssi: RSSIData.create(),
    download_ota: DownloadOTAData.create()
} // Note: perhaps init these as null instead of an undefined create()

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
        }
    }
}

export const telemetry = createTelemetry()
