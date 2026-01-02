<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { onMount } from 'svelte'
    import { socket } from '$lib/stores'
    import { Connection } from '$lib/components/icons'
    import I2CSetting from './i2cSetting.svelte'
    import {
        I2CDevice,
        I2CScanData,
        I2CScanDataRequest
    } from '$lib/platform_shared/websocket_message'

    // TODO: Delete this completely, this should be done on esp side, as it decides what addresses are actually valid, as for example ICM20948 and MPU6050 can have same address
    // const i2cDevices = [
    //     { address: 30, part_number: 'HMC5883', name: '3-Axis Digital Compass/Magnetometer IC' },
    //     { address: 41, part_number: 'BNO055', name: '9-Axis Absolute Orientation Sensor' },
    //     { address: 64, part_number: 'PCA9685', name: '16-channel PWM driver default address' },
    //     { address: 72, part_number: 'ADS1115', name: '4-channel 16-bit ADC' },
    //     {
    //         address: 104,
    //         part_number: 'MPU6050',
    //         name: 'Six-Axis (Gyro + Accelerometer) MEMS MotionTracking™ Devices'
    //     },
    //     { address: 115, part_number: 'PAJ7620U2', name: 'Gesture sensor' },
    //     { address: 119, part_number: 'BMP085', name: 'Temp/Barometric' }
    // ]

    let active_devices: I2CDevice[] = $state([])

    let isLoading = $state(false)

    onMount(() => {
        const unsub = socket.on(I2CScanData, handleScan)
        triggerScan()
        return () => unsub
    })

    const handleScan = (data: I2CScanData) => {
        active_devices = data.devices
        isLoading = false
    }

    const triggerScan = () => {
        isLoading = true
        socket.sendEvent(I2CScanDataRequest, {})
    }
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Connection class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>I<sup>2</sup>C</span>
    {/snippet}
    {#snippet right()}
        <button class="btn btn-primary" onclick={triggerScan} disabled={isLoading}>
            {#if isLoading}
                <span class="loading loading-ring loading-xs"></span>
            {:else}
                Scan
            {/if}
        </button>
    {/snippet}

    <I2CSetting />

    <div class="grid">
        {#if active_devices.length === 0}
            <div>No I2C devices found</div>
        {:else}
            {#each active_devices as device (device.address)}
                <div>[{device.address.toString(16)}] {device.partNumber} - {device.name}</div>
            {/each}
        {/if}
    </div>
</SettingsCard>
