<script lang="ts">
  import { onMount } from 'svelte'
  import { api } from '$lib/api'
  import SettingsCard from '$lib/components/SettingsCard.svelte'
  import { AP, Home, MAC, Devices } from '$lib/components/icons'
  import Spinner from '$lib/components/Spinner.svelte'
  import StatusItem from '$lib/components/StatusItem.svelte'
  import { cubicOut } from 'svelte/easing'
  import { slide } from 'svelte/transition'

  let mdnsStatus: MDNSStatus = $state()
  let services: MDNSServiceItem[] = $state([])
  let isLoading = $state(false)

  const getMDNSStatus = async () => {
    const result = await api.get<MDNSStatus>('/api/mdns/status')
    if (result.isErr()) {
      console.error('Error:', result.inner)
      return
    }
    mdnsStatus = result.inner
  }

  const queryMDNSServices = async () => {
    isLoading = true
    const result = await api.post<MDNSServiceQuery>('/api/mdns/query', {
      service: 'http',
      protocol: 'tcp'
    })
    if (result.isErr()) {
      console.error('Error:', result.inner)
      return
    }
    services = result.inner.services.sort((a, b) => b.name.localeCompare(a.name))
    isLoading = false
  }

  onMount(() => {
    getMDNSStatus()
    queryMDNSServices()
  })

  interface MDNSServiceQuery {
    services: MDNSServiceItem[]
  }

  interface MDNSServiceItem {
    ip: string
    port: number
    name: string
  }

  interface MDNSService {
    service: string
    protocol: string
    port: number
  }

  interface MDNSTxtRecord {
    key: string
    value: string
  }

  interface MDNSStatus {
    started: boolean
    hostname: string
    instance: string
    services: MDNSService[]
    global_txt_records: MDNSTxtRecord[]
  }

  const triggerScan = async () => {
    await queryMDNSServices()
  }
</script>

<SettingsCard collapsible={false}>
  {#snippet icon()}
    <AP class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
  {/snippet}
  {#snippet title()}
    <span>MDNS</span>
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
  <div class="w-full overflow-x-auto">
    {#await getMDNSStatus()}
      <Spinner />
    {:then nothing}
      <div
        class="flex w-full flex-col space-y-1"
        transition:slide|local={{ duration: 300, easing: cubicOut }}>
        <StatusItem icon={Home} title="IP Address" description={mdnsStatus.hostname} />

        <StatusItem icon={MAC} title="Instance" description={mdnsStatus.instance} />

        <StatusItem icon={Devices} title="Services" description={mdnsStatus.services.length} />

        <table class="table">
          <thead>
            <tr>
              <th></th>
              <th>Name</th>
              <th>Ip address</th>
              <th>Port</th>
            </tr>
          </thead>
          <tbody>
            {#each services as service}
              <tr>
                <td><Devices class="h-6 w-6" /></td>
                <td>{service.name}</td>
                <td>{service.ip}</td>
                <td>{service.port}</td>
              </tr>
              <!-- <StatusItem icon={Devices} title={service.name} description={service.port} /> -->
            {/each}
          </tbody>
        </table>
      </div>
    {/await}
  </div>
</SettingsCard>
