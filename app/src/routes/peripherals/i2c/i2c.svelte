<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte';
    import { onMount } from 'svelte';
    import { socket } from '$lib/stores';
    import type { I2CDevice } from '$lib/types/models';
    import { Connection } from '$lib/components/icons';

    const i2cDevices = [
        { address: 30, part_number: 'HMC5883', name: '3-Axis Digital Compass/Magnetometer IC' },
        { address: 64, part_number: 'PCA9685', name: '16-channel PWM driver default address' },
        { address: 72, part_number: 'ADS1115', name: '4-channel 16-bit ADC' },
        {
            address: 104,
            part_number: 'MPU6050',
            name: 'Six-Axis (Gyro + Accelerometer) MEMS MotionTracking™ Devices'
        },
        { address: 119, part_number: 'BMP085', name: 'Temp/Barometric' }
    ];

    let active_devices: I2CDevice[] = $state([]);

    onMount(() => {
        socket.on('i2cScan', handleScan);
        socket.sendEvent('i2cScan', '');
        return () => socket.off('i2cScan', handleScan);
    });

    const handleScan = (data: any) => {
        active_devices = data.addresses.map(
            (address: number) =>
                i2cDevices.find(device => device.address === address) || {
                    address,
                    part_number: 'Unknown',
                    name: 'Unknown'
                }
        );
    };
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Connection  class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span >I<sup>2</sup>C</span>
    {/snippet}

    <div class="grid">
        {#if active_devices.length === 0}
            <div>No I2C devices found</div>
        {:else}
            {#each active_devices as device}
                <div>[{device.address.toString(16)}] {device.part_number} - {device.name}</div>
            {/each}
        {/if}
    </div>
</SettingsCard>
