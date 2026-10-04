<script lang="ts">
    import { modals } from 'svelte-modals'
    import ConfirmDialog from '$lib/components/ConfirmDialog.svelte'
    import { notifications } from '$lib/components/toasts/notifications'
    import { Cancel, Check } from '$lib/components/icons'
    import { variantLabel } from '$lib/stores/robots'
    import type { FeaturesDataResponse } from '$lib/platform_shared/message'
    import { VARIANT_CHOICES, chooseVariant } from '$lib/services/robot-variant'
    import { knownVariant, type Variant } from '$lib/kinematics-variants'

    const { features, class: className = '' }: { features: FeaturesDataResponse; class?: string } =
        $props()

    const connectedVariant = $derived(knownVariant(features.variant))

    const confirmVariant = (variant: Variant) =>
        modals.open(ConfirmDialog, {
            title: `Switch to ${variantLabel(variant)}?`,
            message:
                'The robot drives its legs as this variant from now on. It must be deactivated first; its servo calibration is kept.',
            labels: {
                cancel: { label: 'Cancel', icon: Cancel },
                confirm: { label: 'Switch', icon: Check }
            },
            onConfirm: async () => {
                modals.close()
                const error = await chooseVariant(variant)
                if (error) notifications.error(error, 5000)
            }
        })
</script>

<label class="select select-sm w-full {className}">
    <span class="label">Variant</span>
    <select
        value={connectedVariant ?? ''}
        onchange={event => {
            const chosen = event.currentTarget.value as Variant
            event.currentTarget.value = connectedVariant ?? ''
            confirmVariant(chosen)
        }}
    >
        {#if !connectedVariant}
            <option value="" disabled>
                {variantLabel(features.variant) ?? 'Not chosen'}
            </option>
        {/if}
        {#each VARIANT_CHOICES as variant (variant)}
            <option value={variant}>{variantLabel(variant)}</option>
        {/each}
    </select>
</label>
