<script lang="ts">
  import { modals } from 'svelte-modals';
  import ConfirmDialog from '$lib/components/ConfirmDialog.svelte';
  import SettingsCard from '$lib/components/SettingsCard.svelte';

  import { api } from '$lib/api';
  import { Cancel, OTA, Warning } from '$lib/components/icons';

  let files: FileList | undefined = $state();

  async function uploadBIN() {
    const formData = new FormData();
    formData.append('file', files![0]);
    const result = await api.post('/api/firmware', formData);
    if (result.isErr()) console.error('Error:', result.inner);
  }

  function confirmBinUpload() {
    modals.open(ConfirmDialog, {
      title: 'Confirm Flashing the Device',
      message: 'Are you sure you want to overwrite the existing firmware with a new one?',
      labels: {
        cancel: { label: 'Abort', icon: Cancel },
        confirm: { label: 'Upload', icon: OTA },
      },
      onConfirm: () => {
        modals.close();
        uploadBIN();
      },
    });
  }
</script>

<SettingsCard collapsible={false}>
  {#snippet icon()}
    <OTA class="lex-shrink-0 mr-2 h-6 w-6 self-end rounded-full" />
  {/snippet}
  {#snippet title()}
    <span>Upload Firmware</span>
  {/snippet}
  <div class="alert alert-warning shadow-lg">
    <Warning class="h-6 w-6 shrink-0" />
    <span
      >Uploading a new firmware (.bin) file will replace the existing firmware. You may upload a
      (.md5) file first to verify the uploaded firmware.
    </span>
  </div>

  <input
    type="file"
    id="binFile"
    class="file-input file-input-bordered file-input-secondary mt-4 w-full"
    bind:files
    accept=".bin,.md5"
    onchange={confirmBinUpload} />
</SettingsCard>
