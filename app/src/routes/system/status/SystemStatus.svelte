<script lang="ts">
  import { onDestroy, onMount } from 'svelte'
  import { modals } from 'svelte-modals'
  import ConfirmDialog from '$lib/components/ConfirmDialog.svelte'
  import SettingsCard from '$lib/components/SettingsCard.svelte'
  import Spinner from '$lib/components/Spinner.svelte'
  import { slide } from 'svelte/transition'
  import { cubicOut } from 'svelte/easing'
  import type { SystemInformation, Analytics } from '$lib/types/models'
  import { socket } from '$lib/stores/socket'
  import { api } from '$lib/api'
  import { convertSeconds } from '$lib/utilities'
  import { useFeatureFlags } from '$lib/stores/featureFlags'
  import {
    Cancel,
    Power,
    FactoryReset,
    Sleep,
    Health,
    CPU,
    SDK,
    CPP,
    Speed,
    Heap,
    Pyramid,
    Sketch,
    Flash,
    Folder,
    Temperature,
    Stopwatch
  } from '$lib/components/icons'
  import StatusItem from '$lib/components/StatusItem.svelte'
  import ActionButton from './ActionButton.svelte'

  const features = useFeatureFlags()

  let systemInformation: SystemInformation = $state()

  async function getSystemStatus() {
    const result = await api.get<SystemInformation>('/api/system/status')
    if (result.isErr()) {
      console.error('Error:', result.inner)
      return
    }
    systemInformation = result.inner
    return systemInformation
  }

  const postFactoryReset = async () => await api.post('/api/system/reset')

  const postSleep = async () => await api.post('api/sleep')

  onMount(() => socket.on('analytics', handleSystemData))

  onDestroy(() => socket.off('analytics', handleSystemData))

  const handleSystemData = (data: Analytics) =>
    (systemInformation = { ...systemInformation, ...data })

  const postRestart = async () => await api.post('/api/system/restart')

  function confirmRestart() {
    modals.open(ConfirmDialog, {
      title: 'Confirm Restart',
      message: 'Are you sure you want to restart the device?',
      labels: {
        cancel: { label: 'Abort', icon: Cancel },
        confirm: { label: 'Restart', icon: Power }
      },
      onConfirm: () => {
        modals.close()
        postRestart()
      }
    })
  }

  function confirmReset() {
    modals.open(ConfirmDialog, {
      title: 'Confirm Factory Reset',
      message: 'Are you sure you want to reset the device to its factory defaults?',
      labels: {
        cancel: { label: 'Abort', icon: Cancel },
        confirm: { label: 'Factory Reset', icon: FactoryReset }
      },
      onConfirm: () => {
        modals.close()
        postFactoryReset()
      }
    })
  }

  function confirmSleep() {
    modals.open(ConfirmDialog, {
      title: 'Confirm Going to Sleep',
      message: 'Are you sure you want to put the device into sleep?',
      labels: {
        cancel: { label: 'Abort', icon: Cancel },
        confirm: { label: 'Sleep', icon: Sleep }
      },
      onConfirm: () => {
        modals.close()
        postSleep()
      }
    })
  }

  interface ActionButtonDef {
    icon: any
    label: string
    onClick: () => void
    type?: string
    condition?: () => boolean
  }

  const actionButtons: ActionButtonDef[] = [
    {
      icon: Sleep,
      label: 'Sleep',
      onClick: confirmSleep,
      condition: () => Boolean($features.sleep)
    },
    {
      icon: Power,
      label: 'Restart',
      onClick: confirmRestart
    },
    {
      icon: FactoryReset,
      label: 'Factory Reset',
      onClick: confirmReset,
      type: 'secondary'
    }
  ]
</script>

<SettingsCard collapsible={false}>
  {#snippet icon()}
    <Health class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
  {/snippet}
  {#snippet title()}
    <span>System Status</span>
  {/snippet}

  <div class="w-full overflow-x-auto">
    {#await getSystemStatus()}
      <Spinner />
    {:then nothing}
      <div
        class="flex w-full flex-col space-y-1"
        transition:slide|local={{ duration: 300, easing: cubicOut }}>
        <StatusItem
          icon={CPU}
          title="Chip"
          description={`${systemInformation.cpu_type} Rev ${systemInformation.cpu_rev}`} />

        <StatusItem
          icon={SDK}
          title="SDK Version"
          description={`ESP-IDF ${systemInformation.sdk_version} / Arduino ${systemInformation.arduino_version}`} />

        <StatusItem
          icon={CPP}
          title="Firmware Version"
          description={systemInformation.firmware_version} />

        <StatusItem
          icon={Speed}
          title="CPU Frequency"
          description={`${systemInformation.cpu_freq_mhz} MHz ${
            systemInformation.cpu_cores == 2 ? 'Dual Core' : 'Single Core'
          }`} />

        <StatusItem
          icon={Heap}
          title="Heap (Free / Max Alloc)"
          description={`${systemInformation.free_heap} / ${systemInformation.max_alloc_heap} bytes`} />

        <StatusItem
          icon={Pyramid}
          title="PSRAM (Size / Free)"
          description={`${systemInformation.psram_size} / ${systemInformation.psram_size} bytes`} />

        <StatusItem
          icon={Sketch}
          title="Sketch (Used / Free)"
          description={`${(
            (systemInformation.sketch_size / systemInformation.free_sketch_space) *
            100
          ).toFixed(1)} % of
                ${systemInformation.free_sketch_space / 1000000} MB used (${
                  (systemInformation.free_sketch_space - systemInformation.sketch_size) / 1000000
                } MB free)`} />

        <StatusItem
          icon={Flash}
          title="Flash Chip (Size / Speed)"
          description={`${systemInformation.flash_chip_size / 1000000} MB / ${
            systemInformation.flash_chip_speed / 1000000
          } MHz`} />

        <StatusItem
          icon={Folder}
          title="File System (Used / Total)"
          description={`${((systemInformation.fs_used / systemInformation.fs_total) * 100).toFixed(
            1
          )} % of ${systemInformation.fs_total / 1000000} MB used (${
            (systemInformation.fs_total - systemInformation.fs_used) / 1000000
          }
                MB free)`} />

        <StatusItem
          icon={Temperature}
          title="Core Temperature"
          description={`${
            systemInformation.core_temp == 53.33 ?
              'NaN'
            : systemInformation.core_temp.toFixed(2) + ' °C'
          }`} />

        <StatusItem
          icon={Stopwatch}
          title="Uptime"
          description={convertSeconds(systemInformation.uptime)} />

        <StatusItem
          icon={Power}
          title="Reset Reason"
          description={systemInformation.cpu_reset_reason} />
      </div>
    {/await}
  </div>

  <div class="mt-4 flex flex-wrap justify-end gap-2">
    {#each actionButtons as button}
      {#if button.condition === undefined || button.condition()}
        <ActionButton
          on:click={button.onClick}
          icon={button.icon}
          label={button.label}
          type={button.type || 'primary'} />
      {/if}
    {/each}
  </div>
</SettingsCard>
