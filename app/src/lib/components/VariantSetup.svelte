<script lang="ts">
    import { focusTrap } from 'svelte-focus-trap'
    import { fade, fly } from 'svelte/transition'
    import { notifications } from '$lib/components/toasts/notifications'
    import { variantLabel } from '$lib/stores/robots'
    import {
        VARIANT_CHOICES,
        chooseVariant,
        restartingAs,
        variantSetupStep
    } from '$lib/services/robot-variant'
    import type { Variant } from '$lib/kinematics-variants'

    let choosing = $state<Variant | null>(null)

    const choose = async (variant: Variant) => {
        choosing = variant
        const error = await chooseVariant(variant)
        if (error) notifications.error(error, 5000)
        choosing = null
    }
</script>

{#if $variantSetupStep}
    <div
        class="fixed inset-0 z-40 bg-black/20 backdrop-blur-sm"
        transition:fade
        role="presentation"
    ></div>
    <div
        role="dialog"
        aria-modal="true"
        aria-labelledby="variant-setup-title"
        class="pointer-events-none fixed inset-0 z-50 flex items-center justify-center p-4"
        transition:fly={{ y: 50 }}
        use:focusTrap
    >
        <div
            class="rounded-box bg-base-100 pointer-events-auto flex w-full max-w-md flex-col p-4 shadow-lg"
        >
            {#if $variantSetupStep === 'restarting'}
                <h2 id="variant-setup-title" class="text-2xl font-bold">Restarting...</h2>
                <div class="divider my-2"></div>
                <p class="flex items-center gap-3">
                    <span class="loading loading-spinner loading-sm shrink-0"></span>
                    The robot restarts as {variantLabel($restartingAs)}; the connection returns a
                    few seconds later.
                </p>
            {:else}
                <h2 id="variant-setup-title" class="text-2xl font-bold">Which robot is this?</h2>
                <div class="divider my-2"></div>
                <p class="mb-4">
                    The robot moves only once it knows its build. It restarts after you choose.
                </p>
                <div class="flex flex-col gap-2">
                    {#each VARIANT_CHOICES as variant (variant)}
                        <button
                            class="btn btn-outline btn-primary justify-start"
                            disabled={choosing !== null}
                            onclick={() => choose(variant)}
                        >
                            {#if choosing === variant}
                                <span class="loading loading-spinner loading-xs"></span>
                            {/if}
                            {variantLabel(variant)}
                        </button>
                    {/each}
                </div>
            {/if}
        </div>
    </div>
{/if}
