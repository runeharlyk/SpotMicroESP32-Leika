<script lang="ts">
    import { onMount } from 'svelte'
    import { api } from '$lib/api'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { AP, Home, MAC, Devices } from '$lib/components/icons'
    import StatusItem from '$lib/components/StatusItem.svelte'
    import { cubicOut } from 'svelte/easing'
    import { slide } from 'svelte/transition'
    import { compareIp } from '$lib/utilities'
    import {
        type MDNSStatusData,
        type MDNSQueryResult,
        type MDNSQueryRequest,
        type MDNSQueryResponse,
        MDNSStatusData as MDNSStatusDataProto,
        MDNSQueryRequest as MDNSQueryRequestProto,
        MDNSQueryResponse as MDNSQueryResponseProto
    } from '$lib/platform_shared/message'

    let mdnsStatus: MDNSStatusData | undefined = $state()
    let services: MDNSQueryResult[] = $state([])
    let isLoading = $state(false)

    const getMDNSStatus = async () => {
        const result = await api.get('/api/mdns/status', MDNSStatusDataProto)
        if (result.isErr()) {
            console.error('Error:', result.inner)
            return
        }
        mdnsStatus = result.inner
    }

    const queryMDNSServices = async () => {
        isLoading = true
        const request: MDNSQueryRequest = { service: 'http', protocol: 'tcp' }
        const result = await api.post(
            '/api/mdns/query',
            request,
            MDNSQueryRequestProto,
            MDNSQueryResponseProto
        )
        if (result.isErr()) {
            console.error('Error:', result.inner)
            return
        }
        services = result.inner.services.sort((a, b) => compareIp(a.address, b.address))
        isLoading = false
    }

    onMount(async () => {
        await getMDNSStatus()
        await queryMDNSServices()
    })

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
        {#if mdnsStatus}
            <div
                class="flex w-full flex-col space-y-1"
                transition:slide|local={{ duration: 300, easing: cubicOut }}
            >
                <StatusItem icon={Home} title="IP Address" description={mdnsStatus.hostname} />

                <StatusItem icon={MAC} title="Instance" description={mdnsStatus.instance} />

                <StatusItem
                    icon={Devices}
                    title="Services"
                    description={mdnsStatus.services.length}
                />

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
                        {#each services as service (service.address)}
                            <tr>
                                <td><Devices class="h-6 w-6" /></td>
                                <td>{service.hostname}</td>
                                <td>{service.address}</td>
                                <td>{service.port}</td>
                            </tr>
                        {/each}
                    </tbody>
                </table>
            </div>
        {/if}
    </div>
</SettingsCard>
