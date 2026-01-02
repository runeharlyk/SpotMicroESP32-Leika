<script lang="ts">
    import { ModeData, ModesEnum } from '$lib/platform_shared/websocket_message'
    import { mode, modes } from '$lib/stores'

    const deactivate = async () => {
        mode.set(ModeData.create({ mode: ModesEnum.DEACTIVATED }))
    }
</script>

<button onclick={deactivate} class="bg-error text-white btn rounded-none">STOP</button>
