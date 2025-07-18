export enum Topics {
  imu = 'imu',
  mode = 'mode',
  input = 'input',
  analytics = 'analytics',
  position = 'position',
  angles = 'angles',
  i2cScan = 'i2cScan',
  peripheralSettings = 'peripheralSettings',
  otastatus = 'otastatus',
  servoState = 'servoState',
  servoPWM = 'servoPWM',
  WiFiSettings = 'WiFiSettings',
  sonar = 'sonar',
  rssi = 'rssi'
}

export type vector = { x: number; y: number }

export interface ControllerInput {
  left: vector
  right: vector
  height: number
  speed: number
  s1: number
}

export type GithubRelease = {
  message: string
  tag_name: string
  assets: Array<{
    name: string
    browser_download_url: string
  }>
}

export type angles = number[] | Int16Array

export type WifiStatus = {
  status: number
  local_ip: string
  mac_address: string
  rssi: number
  ssid: string
  bssid: string
  channel: number
  subnet_mask: string
  gateway_ip: string
  dns_ip_1: string
  dns_ip_2?: string
}

export type WifiSettings = {
  hostname: string
  priority_RSSI: boolean
  wifi_networks: KnownNetworkItem[]
}

export type NetworkList = {
  networks: NetworkItem[]
}

export type KnownNetworkItem = {
  ssid: string
  password: string
  static_ip_config: boolean
  local_ip?: string
  subnet_mask?: string
  gateway_ip?: string
  dns_ip_1?: string
  dns_ip_2?: string
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
  ip_address: string
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
  local_ip: string
  gateway_ip: string
  subnet_mask: string
}

export type DownloadOTA = {
  status: string
  progress: number
  error: string
}

export type Analytics = {
  max_alloc_heap: number
  psram_size: number
  free_psram: number
  free_heap: number
  total_heap: number
  min_free_heap: number
  core_temp: number
  fs_total: number
  fs_used: number
  uptime: number
  cpu0_usage: number
  cpu1_usage: number
  cpu_usage: number
}

export type Rssi = {
  rssi: number
  ssid: string
}

export type StaticSystemInformation = {
  esp_platform: string
  firmware_version: string
  cpu_freq_mhz: number
  cpu_type: string
  cpu_rev: number
  cpu_cores: number
  sketch_size: number
  free_sketch_space: number
  sdk_version: string
  arduino_version: string
  flash_chip_size: number
  flash_chip_speed: number
  cpu_reset_reason: string
}

export type SystemInformation = Analytics & StaticSystemInformation

export type IMU = {
  x: number
  y: number
  z: number
  heading: number
  altitude: number
  bmp_temp: number
  pressure: number
}

export interface I2CDevice {
  address: number
  part_number: string
  name: string
}

export type PinConfig = {
  pin: number
  mode: string
  type: string
  role: string
}

export type PeripheralsConfiguration = {
  sda: number
  scl: number
  frequency: number
  pins: PinConfig[]
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

export type File = number

export interface Directory {
  [key: string]: File | Directory
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
