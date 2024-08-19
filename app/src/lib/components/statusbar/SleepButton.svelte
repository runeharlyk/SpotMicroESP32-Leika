<script lang="ts">
	import { useFeatureFlags } from '$lib/stores';
	import { closeModal, openModal } from 'svelte-modals';
    import ConfirmDialog from '$lib/components/ConfirmDialog.svelte';
	import { api } from '$lib/api';
	import { Cancel, Power } from '../icons';

    const features = useFeatureFlags();

    const postSleep = async () => await api.post('/api/sleep');

    const confirmSleep = () => {
		openModal(ConfirmDialog, {
			title: 'Confirm Power Down',
			message: 'Are you sure you want to switch off the device?',
			labels: {
				cancel: { label: 'Abort', icon: Cancel },
				confirm: { label: 'Switch Off', icon: Power }
			},
			onConfirm: () => {
				closeModal();
				postSleep();
			}
		});
	}
</script>

{#if $features.sleep}
    <div class="flex-none">
        <button class="btn btn-square btn-ghost h-9 w-10" on:click={confirmSleep}>
            <Power class="text-error h-9 w-9" />
        </button>
    </div>
{/if}