import type { PowerData } from '$lib/platform_shared/message'

export const volts = (value: number) => `${value.toFixed(2)} V`
export const amps = (value: number) => `${value.toFixed(2)} A`
export const watts = (value: number) => `${value.toFixed(1)} W`

/** The lowest rail voltage among the readings, which shows a dip a single reading has already left behind. */
export const lowestVoltage = (readings: PowerData[]): number | undefined =>
    readings.length ? Math.min(...readings.map(reading => reading.busVoltage)) : undefined

/** What the robot reports it did about the rail, most urgent first. */
export const powerWarnings = (reading: PowerData | undefined): string[] => [
    ...(reading?.lowVoltage ?
        ['The rail is under the cut-off voltage: the robot is held deactivated']
    :   []),
    ...(reading?.stalled ? ['The servos draw more than the stall current'] : [])
]
