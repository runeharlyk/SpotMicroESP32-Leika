import { derived, writable } from 'svelte/store'
import { socket } from '$lib/stores/socket'
import { applyFeatures, connectionFeatures, variantChoiceNeeded } from '$lib/stores/featureFlags'
import { KinematicsVariant } from '$lib/platform_shared/message'
import { VARIANT_DIMENSIONS, knownVariant, type Variant } from '$lib/kinematics-variants'

export const VARIANT_CHOICES = Object.keys(VARIANT_DIMENSIONS) as Variant[]

/**
 * The variant the robot was told to restart as, until it next reports its features. Its reply
 * still reports the variant it runs as, so only the reconnect shows whether the change took.
 */
export const restartingAs = writable<Variant | null>(null)

connectionFeatures.subscribe(features => {
    if (features) restartingAs.set(null)
})

/**
 * Tells the connected robot which variant it is; resolves to an error message, or null on success.
 * A robot whose variant changes restarts right after its reply, so the connection drops and returns.
 */
export async function chooseVariant(variant: Variant): Promise<string | null> {
    try {
        const response = await socket.request({
            robotVariantUpdate: { variant: KinematicsVariant[variant] }
        })
        if (response.featuresDataResponse) applyFeatures(response.featuresDataResponse)
        if (response.statusCode !== 200)
            return response.errorMessage || `The robot rejected the variant ${variant}`
        if (knownVariant(response.featuresDataResponse?.variant) !== variant)
            restartingAs.set(variant)
        return null
    } catch (error) {
        return error instanceof Error ? error.message : String(error)
    }
}

type SetupStep = 'choose' | 'restarting' | null

export const variantSetupStep = derived(
    [variantChoiceNeeded, restartingAs],
    ([$needed, $restartingAs]): SetupStep =>
        $restartingAs ? 'restarting'
        : $needed ? 'choose'
        : null
)
