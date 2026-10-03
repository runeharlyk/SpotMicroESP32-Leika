import { socket } from '$lib/stores/socket'
import { applyFeatures } from '$lib/stores/featureFlags'
import { KinematicsVariant } from '$lib/platform_shared/message'
import { VARIANT_DIMENSIONS, type Variant } from '$lib/kinematics-variants'

export const VARIANT_CHOICES = Object.keys(VARIANT_DIMENSIONS) as Variant[]

/**
 * Tells the connected robot which variant it is; resolves to an error message, or null on success.
 * The robot switches while its legs are at rest and replies once it has, so the reply reports the new
 * variant; it refuses while a mode moves the legs.
 */
export async function chooseVariant(variant: Variant): Promise<string | null> {
    try {
        const response = await socket.request({
            robotVariantUpdate: { variant: KinematicsVariant[variant] }
        })
        if (response.featuresDataResponse) applyFeatures(response.featuresDataResponse)
        if (response.statusCode === 409) return 'Deactivate the robot before changing its variant'
        if (response.statusCode !== 200)
            return response.errorMessage || `The robot rejected the variant ${variant}`
        return null
    } catch (error) {
        return error instanceof Error ? error.message : String(error)
    }
}
