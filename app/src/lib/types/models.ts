export enum MessageTopic {
    imu = 'imu',
    imuCalibrate = 'imuCalibrate',
    mode = 'mode',
    input = 'input',
    analytics = 'analytics',
    position = 'position',
    angles = 'angles',
    i2cScan = 'i2cScan',
    peripheralSettings = 'peripheralSettings',
    otastatus = 'otastatus',
    gait = 'walk_gait',
    servoState = 'servoState',
    servoPWM = 'servoPWM',
    WiFiSettings = 'WiFiSettings',
    sonar = 'sonar',
    rssi = 'rssi'
}

export type WifiStatus = {
    status: number
    local_ip: number
    mac_address: string
    rssi: number
    ssid: string
    bssid: string
    channel: number
    subnet_mask: number
    gateway_ip: number
    dns_ip_1: number
    dns_ip_2?: number
}

export type KnownNetworkItem = {
    ssid: string
    password: string
    static_ip_config: boolean
    local_ip?: number
    subnet_mask?: number
    gateway_ip?: number
    dns_ip_1?: number
    dns_ip_2?: number
}

export type WifiSettings = {
    hostname: string
    priority_RSSI: boolean
    wifi_networks: KnownNetworkItem[]
}

export type vector = { x: number; y: number }

export type GithubRelease = {
    message: string
    tag_name: string
    assets: Array<{
        name: string
        browser_download_url: string
    }>
}

export type NetworkList = {
    networks: NetworkItem[]
}

export type NetworkItem = {
    rssi: number
    ssid: string
    bssid: string
    channel: number
    encryption_type: number
}

export type ApStatus = {
    status: number
    ip_address: number
    mac_address: string
    station_num: number
}

export type ApSettings = {
    provision_mode: number
    ssid: string
    password: string
    channel: number
    ssid_hidden: boolean
    max_clients: number
    local_ip: number
    gateway_ip: number
    subnet_mask: number
}

export type Rssi = {
    rssi: number
    ssid: string
}

export type CameraSettings = {
    framesize: number
    quality: number
    brightness: number
    contrast: number
    saturation: number
    sharpness: number
    denoise: number
    special_effect: number
    wb_mode: number
    vflip: boolean
    hmirror: boolean
}


export type Servo = {
    name: string
    channel: number
    inverted: boolean
    angle: number
    center_angle: number
}

export type ServoConfiguration = {
    is_active: boolean
    servo_pwm_frequency: number
    servo_oscillator_frequency: number
    servos: Servo[]
}

export interface MDNSServiceQuery {
    services: MDNSServiceItem[]
}

export interface MDNSServiceItem {
    ip: string
    port: number
    name: string
}

export interface MDNSService {
    service: string
    protocol: string
    port: number
}

export interface MDNSTxtRecord {
    key: string
    value: string
}

export interface MDNSStatus {
    started: boolean
    hostname: string
    instance: string
    services: MDNSService[]
    global_txt_records: MDNSTxtRecord[]
}
