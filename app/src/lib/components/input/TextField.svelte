<script lang="ts">
    import type { Snippet } from 'svelte'

    interface Props {
        id: string
        label: string
        /** Shown under the field, which is marked invalid while it is set. */
        error?: string
        value?: string | number
        numeric?: boolean
        placeholder?: string
        /** Replaces the plain input, e.g. with a password input bound to the same value. */
        input?: Snippet
    }

    let {
        id,
        label,
        error,
        value = $bindable(''),
        numeric = false,
        placeholder,
        input
    }: Props = $props()

    const invalid = $derived(error ? 'border-error border-2' : '')
</script>

<div>
    <label class="label" for={id}><span class="label-text text-md">{label}</span></label>
    {#if input}
        {@render input()}
    {:else if numeric}
        <input
            {id}
            type="number"
            class="input input-bordered w-full {invalid}"
            aria-invalid={!!error}
            {placeholder}
            bind:value
        />
    {:else}
        <input
            {id}
            type="text"
            class="input input-bordered w-full {invalid}"
            aria-invalid={!!error}
            {placeholder}
            bind:value
        />
    {/if}
    {#if error}
        <label class="label" for={id}
            ><span class="label-text-alt text-error whitespace-normal">{error}</span></label
        >
    {/if}
</div>
