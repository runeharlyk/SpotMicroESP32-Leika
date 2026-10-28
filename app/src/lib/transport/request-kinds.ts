import type { CorrelationRequest } from '$lib/platform_shared/message'

export type RequestKey = Exclude<keyof CorrelationRequest, 'correlationId'>

/**
 * What a request is, which decides what becomes of it while no link is up (socket.request). An
 * action has a physical effect or ends something under way, and fails at once, since the robot it
 * was meant for may have moved on by the time a link comes. A setting is a stored write: it waits
 * SETTING_GRACE_MS, so a link blip goes unnoticed, and then fails as not saved. A read waits for a
 * link until the request's timeout.
 */
export type RequestKind = 'action' | 'setting' | 'read'

/** How long a settings write waits for a link before it fails. */
export const SETTING_GRACE_MS = 3000

/** Every request, assigned once: a request the schema gains does not compile until it is given a kind here. */
export const REQUEST_KINDS: Record<RequestKey, RequestKind> = {
    featuresDataRequest: 'read',
    i2cScanDataRequest: 'read',
    imuCalibrateExecute: 'action',
    systemInformationRequest: 'read',
    fsDeleteRequest: 'setting',
    fsMkdirRequest: 'setting',
    fsListRequest: 'read',
    fsDownloadRequest: 'read',
    fsUploadStart: 'setting',
    fsCancelTransfer: 'action',
    robotNameUpdate: 'setting',
    robotVariantUpdate: 'setting',
    wifiSettingsRequest: 'read',
    wifiSettings: 'setting',
    wifiStatusRequest: 'read',
    // A scan only listens; it moves nothing and stores nothing.
    wifiScanStart: 'read',
    wifiNetworksRequest: 'read',
    apSettingsRequest: 'read',
    apSettings: 'setting',
    apStatusRequest: 'read',
    mdnsStatusRequest: 'read',
    mdnsQueryRequest: 'read',
    cameraSettingsRequest: 'read',
    cameraSettings: 'setting',
    servoSettingsRequest: 'read',
    servoSettings: 'setting',
    peripheralSettingsRequest: 'read',
    peripheralSettings: 'setting',
    systemRestart: 'action',
    systemReset: 'action',
    otaStart: 'action',
    otaChunk: 'action',
    otaFinish: 'action',
    otaDownload: 'action',
    animationListRequest: 'read',
    animationValidate: 'read',
    animationPlay: 'action',
    animationStop: 'action',
    animationPose: 'action',
    coreDumpRequest: 'read',
    coreDumpDownload: 'read',
    coreDumpErase: 'action',
    // With `reset` it zeroes the pose, which moves nothing.
    odometryRequest: 'read',
    goalMove: 'action',
    goalCancel: 'action',
    goalStatusRequest: 'read'
}

/** The kind of the request a payload carries. */
export const kindOfRequest = (payload: Omit<CorrelationRequest, 'correlationId'>): RequestKind => {
    const key = (Object.keys(payload) as RequestKey[]).find(k => payload[k] !== undefined)
    return key ? REQUEST_KINDS[key] : 'read'
}
