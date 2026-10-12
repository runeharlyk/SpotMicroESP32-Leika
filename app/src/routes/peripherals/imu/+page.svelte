<script lang="ts">
    import IMU from './imu.svelte'
    import Sonar from './Sonar.svelte'
    import Power from './Power.svelte'
    import { useFeatureFlags } from '$lib/stores/featureFlags'

    const features = useFeatureFlags()
</script>

<div class="mx-0 my-1 flex flex-col space-y-4 sm:mx-8 sm:my-8">
    <IMU />
    {#if $features.sonar}
        <Sonar />
    {/if}
    {#if $features.power}
        <Power />
    {/if}
</div>
