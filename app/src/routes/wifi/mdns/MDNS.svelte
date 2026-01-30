<script lang="ts">
    import { onMount } from 'svelte'
    import { api } from '$lib/api'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { AP, Home, MAC, Devices } from '$lib/components/icons'
    import StatusItem from '$lib/components/StatusItem.svelte'
    import { cubicOut } from 'svelte/easing'
    import { slide } from 'svelte/transition'
    import {
        type MDNSStatus,
        type MDNSQueryResult,
        Request,
        type Response as ProtoResponse
    } from '$lib/platform_shared/api'
    import { compareIp } from '$lib/utilities'

    let mdnsStatus = $state<MDNSStatus | undefined>()
    let services = $state<MDNSQueryResult[]>([])
    let isLoading = $state(false)

    const getMDNSStatus = async () => {
        const result = await api.get<ProtoResponse>('/api/mdns/status')
        if (result.isErr()) {
            console.error('Error:', result.inner)
            return
        }
        if (result.inner.mdnsStatus) {
            mdnsStatus = result.inner.mdnsStatus
        }
    }

    const queryMDNSServices = async () => {
        isLoading = true
        const request = Request.create({
            mdnsQueryRequest: {
                service: 'http',
                protocol: 'tcp'
            }
        })
        const result = await api.post_proto<ProtoResponse>('/api/mdns/query', request)
        if (result.isErr()) {
            console.error('Error:', result.inner)
            isLoading = false
            return
        }
        if (result.inner.mdnsQueryResponse) {
            services = result.inner.mdnsQueryResponse.services.sort((a, b) => compareIp(a.ip, b.ip))
        }
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
                        {#each services as service (service.ip)}
                            <tr>
                                <td><Devices class="h-6 w-6" /></td>
                                <td>{service.name}</td>
                                <td>{service.ip}</td>
                                <td>{service.port}</td>
                            </tr>
                        {/each}
                    </tbody>
                </table>
            </div>
        {/if}
    </div>
</SettingsCard>
