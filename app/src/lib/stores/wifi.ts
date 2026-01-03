import { writable, derived, type Readable } from 'svelte/store'
import { socket } from './socket'
import {
    WifiSettingsData,
    WifiSettingsDataRequest,
    NetworkStatusData,
    WifiStatusDataRequest,
    APSettingsData,
    APSettingsDataRequest,
    APStatusData,
    APStatusDataRequest,
    type WifiSettingsData as WifiSettingsDataType,
    type NetworkStatusData as NetworkStatusDataType,
    type APSettingsData as APSettingsDataType,
    type APStatusData as APStatusDataType
} from '$lib/platform_shared/message'

function createWifiSettingsStore() {
    const { subscribe, set, update } = writable<WifiSettingsDataType | null>(null)
    let unsubscribe: (() => void) | null = null

    function init() {
        unsubscribe = socket.on(WifiSettingsData, (data: WifiSettingsDataType) => {
            set(data)
        })
    }

    function fetch() {
        socket.emit(WifiSettingsDataRequest, {})
    }

    function save(settings: WifiSettingsDataType) {
        socket.emit(WifiSettingsData, settings)
    }

    function destroy() {
        if (unsubscribe) {
            unsubscribe()
            unsubscribe = null
        }
    }

    return {
        subscribe,
        init,
        fetch,
        save,
        destroy,
        set,
        update
    }
}

function createWifiStatusStore() {
    const { subscribe, set } = writable<NetworkStatusDataType | null>(null)
    let unsubscribe: (() => void) | null = null

    function init() {
        unsubscribe = socket.on(NetworkStatusData, (data: NetworkStatusDataType) => {
            set(data)
        })
    }

    function fetch() {
        socket.emit(WifiStatusDataRequest, {})
    }

    function destroy() {
        if (unsubscribe) {
            unsubscribe()
            unsubscribe = null
        }
    }

    return {
        subscribe,
        init,
        fetch,
        destroy
    }
}

function createAPSettingsStore() {
    const { subscribe, set, update } = writable<APSettingsDataType | null>(null)
    let unsubscribe: (() => void) | null = null

    function init() {
        unsubscribe = socket.on(APSettingsData, (data: APSettingsDataType) => {
            set(data)
        })
    }

    function fetch() {
        socket.emit(APSettingsDataRequest, {})
    }

    function save(settings: APSettingsDataType) {
        socket.emit(APSettingsData, settings)
    }

    function destroy() {
        if (unsubscribe) {
            unsubscribe()
            unsubscribe = null
        }
    }

    return {
        subscribe,
        init,
        fetch,
        save,
        destroy,
        set,
        update
    }
}

function createAPStatusStore() {
    const { subscribe, set } = writable<APStatusDataType | null>(null)
    let unsubscribe: (() => void) | null = null

    function init() {
        unsubscribe = socket.on(APStatusData, (data: APStatusDataType) => {
            set(data)
        })
    }

    function fetch() {
        socket.emit(APStatusDataRequest, {})
    }

    function destroy() {
        if (unsubscribe) {
            unsubscribe()
            unsubscribe = null
        }
    }

    return {
        subscribe,
        init,
        fetch,
        destroy
    }
}

export const wifiSettings = createWifiSettingsStore()
export const wifiStatus = createWifiStatusStore()
export const apSettings = createAPSettingsStore()
export const apStatus = createAPStatusStore()

