import type { FeaturesDataResponse } from '$lib/platform_shared/message'
import type { LedSettings, PeripheralSettings, SonarSettings } from '$lib/platform_shared/api'

export type ToggleableSensor = 'imu' | 'mag' | 'bmp' | 'gesture' | 'camera'
type Sensor = ToggleableSensor | 'servo'
type DisabledFlag = `${ToggleableSensor}Disabled`

interface SensorDefinition {
    sensor: Sensor
    label: string
    toggle: ToggleableSensor | null
    /** The camera driver is not torn down while running, so its toggle waits for a restart. */
    appliesOnRestart: boolean
}

const SENSORS: SensorDefinition[] = [
    { sensor: 'imu', label: 'IMU', toggle: 'imu', appliesOnRestart: false },
    { sensor: 'mag', label: 'Magnetometer', toggle: 'mag', appliesOnRestart: false },
    { sensor: 'bmp', label: 'Barometer', toggle: 'bmp', appliesOnRestart: false },
    { sensor: 'gesture', label: 'Gesture sensor', toggle: 'gesture', appliesOnRestart: false },
    { sensor: 'camera', label: 'Camera', toggle: 'camera', appliesOnRestart: true },
    { sensor: 'servo', label: 'Servo board', toggle: null, appliesOnRestart: false }
]

const TOGGLEABLE: ToggleableSensor[] = ['imu', 'mag', 'bmp', 'gesture', 'camera']

const disabledFlag = (sensor: ToggleableSensor): DisabledFlag => `${sensor}Disabled`

/** A disabled sensor may not be probed at all, so not detecting it says nothing about whether it is there. */
export type SensorStatus = 'active' | 'detected-but-disabled' | 'disabled' | 'absent'

export const SENSOR_STATUS_LABELS: Record<SensorStatus, string> = {
    active: 'Active',
    'detected-but-disabled': 'Detected but disabled',
    disabled: 'Disabled',
    absent: 'Not detected'
}

export interface SensorRow extends SensorDefinition {
    status: SensorStatus
}

const sensorStatus = (active: boolean, detected: boolean, disabled: boolean): SensorStatus =>
    active ? 'active'
    : detected ? 'detected-but-disabled'
    : disabled ? 'disabled'
    : 'absent'

/** Each sensor as the robot last reported it; the stored disabled flag only tells apart what it did not detect. */
export const sensorRows = (
    features: FeaturesDataResponse,
    settings: PeripheralSettings
): SensorRow[] =>
    SENSORS.map(definition => ({
        ...definition,
        status: sensorStatus(
            features[definition.sensor],
            features[`${definition.sensor}Detected`],
            definition.toggle !== null && settings[disabledFlag(definition.toggle)]
        )
    }))

export interface PeripheralsForm {
    use: Record<ToggleableSensor, boolean>
    /** Undefined when the robot did not report it, so a save leaves the stored one alone. */
    ws2812: LedSettings | undefined
    espnowController: boolean
    /** As the LEDs: undefined when the robot did not report them. */
    leftSonar: SonarSettings | undefined
    rightSonar: SonarSettings | undefined
}

export const formFromSettings = (settings: PeripheralSettings): PeripheralsForm => ({
    use: Object.fromEntries(
        TOGGLEABLE.map(sensor => [sensor, !settings[disabledFlag(sensor)]])
    ) as Record<ToggleableSensor, boolean>,
    ws2812: settings.ws2812 && { ...settings.ws2812 },
    espnowController: settings.espnowController,
    leftSonar: settings.leftSonar && { ...settings.leftSonar },
    rightSonar: settings.rightSonar && { ...settings.rightSonar }
})

/** The whole message to save: the robot takes the disabled flags as sent, so every one is set. */
export const settingsFromForm = (
    base: PeripheralSettings,
    form: PeripheralsForm
): PeripheralSettings => ({
    ...base,
    ...(Object.fromEntries(
        TOGGLEABLE.map(sensor => [disabledFlag(sensor), !form.use[sensor]])
    ) as Record<DisabledFlag, boolean>),
    ws2812: form.ws2812 && { ...form.ws2812 },
    espnowController: form.espnowController,
    leftSonar: form.leftSonar && { ...form.leftSonar },
    rightSonar: form.rightSonar && { ...form.rightSonar }
})

const validPin = (pin: number) => Number.isInteger(pin) && pin >= 0

const i2cPinName = (base: PeripheralSettings, pin: number) =>
    pin === base.sda ? 'SDA'
    : pin === base.scl ? 'SCL'
    : null

const ledProblem = (base: PeripheralSettings, led: LedSettings | undefined): string | null => {
    if (!led?.enabled) return null
    if (!validPin(led.pin)) return 'The WS2812 LEDs need a pin number'
    const i2c = i2cPinName(base, led.pin)
    return i2c && `The WS2812 LEDs and I2C ${i2c} both use pin ${led.pin}`
}

const sonarPins = (sonar: SonarSettings | undefined) =>
    sonar?.enabled ? [sonar.triggerPin, sonar.echoPin] : []

const sonarProblem = (
    base: PeripheralSettings,
    form: PeripheralsForm,
    name: 'left' | 'right'
): string | null => {
    const sonar = name === 'left' ? form.leftSonar : form.rightSonar
    if (!sonar?.enabled) return null
    const label = `The ${name} sonar`
    if (!validPin(sonar.triggerPin) || !validPin(sonar.echoPin))
        return `${label} needs a trigger and an echo pin`
    if (sonar.triggerPin === sonar.echoPin) return `${label} needs its trigger and echo on two pins`
    const other = sonarPins(name === 'left' ? form.rightSonar : form.leftSonar)
    for (const pin of [sonar.triggerPin, sonar.echoPin]) {
        const i2c = i2cPinName(base, pin)
        if (i2c) return `${label} and I2C ${i2c} both use pin ${pin}`
        if (form.ws2812?.enabled && form.ws2812.pin === pin)
            return `${label} and the WS2812 LEDs both use pin ${pin}`
        if (other.includes(pin)) return `The two sonars both use pin ${pin}`
    }
    return null
}

/**
 * Why the form cannot be saved, or null; the I2C pins are edited on the I2C page and only checked against here. The
 * robot also refuses pins its chip cannot use or another driver holds, which only it knows.
 */
export const formProblem = (base: PeripheralSettings, form: PeripheralsForm): string | null =>
    ledProblem(base, form.ws2812) ??
    sonarProblem(base, form, 'left') ??
    sonarProblem(base, form, 'right')
