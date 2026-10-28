import { SonarStatus, type SonarReading } from '$lib/platform_shared/message'

/** A sonar's last ping as the app shows it: a distance, or why there is none. */
export const sonarText = (reading: SonarReading | undefined): string => {
    switch (reading?.status) {
        case SonarStatus.SONAR_DISTANCE:
            return `${Math.round(reading.distanceCm)} cm`
        case SonarStatus.SONAR_NO_ECHO:
            return 'No echo within 4 m'
        case SonarStatus.SONAR_NO_SENSOR:
            return 'No sensor answers'
        default:
            return 'Off'
    }
}
