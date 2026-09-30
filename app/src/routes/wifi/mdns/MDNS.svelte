<script lang="ts">
    import { robotRequest } from '$lib/robot-request'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import Spinner from '$lib/components/Spinner.svelte'
    import LoadError from '$lib/components/LoadError.svelte'
    import { notifications } from '$lib/components/toasts/notifications'
    import { AP, Home, MAC, Devices } from '$lib/components/icons'
    import StatusItem from '$lib/components/StatusItem.svelte'
    import { cubicOut } from 'svelte/easing'
    import { slide } from 'svelte/transition'
    import type { MDNSStatus, MDNSQueryResult } from '$lib/platform_shared/api'
    import { compareIp } from '$lib/utilities'

    let mdnsStatus = $state<MDNSStatus | undefined>()
    let services = $state<MDNSQueryResult[]>([])
    let isLoading = $state(false)

    const getMDNSStatus = async () => {
        const reply = await robotRequest({ mdnsStatusRequest: {} })
        if (!reply.mdnsStatus) throw new Error('The robot sent no mDNS status')
        mdnsStatus = reply.mdnsStatus
    }

    const queryMDNSServices = async () => {
        isLoading = true
        try {
            const reply = await robotRequest({
                mdnsQueryRequest: { service: 'http', protocol: 'tcp' }
            })
            if (reply.mdnsQueryResponse) {
                services = reply.mdnsQueryResponse.services.sort((a, b) => compareIp(a.ip, b.ip))
            }
        } catch (error) {
            notifications.error(`mDNS scan failed: ${(error as Error).message}`, 5000)
        }
        isLoading = false
    }

    const load = async () => {
        await getMDNSStatus()
        await queryMDNSServices()
    }

    let loading = $state(load())
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <AP class="shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>MDNS</span>
    {/snippet}
    {#snippet right()}
        <button class="btn btn-primary" onclick={queryMDNSServices} disabled={isLoading}>
            {#if isLoading}
                <span class="loading loading-ring loading-xs"></span>
            {:else}
                Scan
            {/if}
        </button>
    {/snippet}
    <div class="w-full overflow-x-auto">
        {#await loading}
            <Spinner />
        {:catch error}
            <LoadError {error} retry={() => (loading = load())} />
        {/await}
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
