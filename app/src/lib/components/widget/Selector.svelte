<script lang="ts">
    interface Props {
        options?: string[];
        selectedOption?: string;
        change: () => void;
        [key: string]: any;
    }

    let { options = [], selectedOption = $bindable(''), ...rest }: Props = $props();
</script>

<select
    bind:value={selectedOption}
    {...rest}
    class="select select-bordered select-sm lg:select-md max-w-xs {rest.class || ''}"
>
    {#each options as option}
        <option value={option}>{option}</option>
    {/each}
</select>
