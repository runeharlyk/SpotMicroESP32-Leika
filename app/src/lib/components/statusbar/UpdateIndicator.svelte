<script lang="ts">
  import { page } from '$app/state';
  import { modals } from 'svelte-modals';
  import { notifications } from '$lib/components/toasts/notifications';
  import ConfirmDialog from '$lib/components/ConfirmDialog.svelte';
  import GithubUpdateDialog from '$lib/components/GithubUpdateDialog.svelte';
  import { compareVersions } from 'compare-versions';
  import { onMount } from 'svelte';
  import { api } from '$lib/api';
  import type { GithubRelease } from '$lib/types/models';
  import { useFeatureFlags } from '$lib/stores/featureFlags';
  import { Cancel, CloudDown, Firmware } from '../icons';

  const features = useFeatureFlags();

  interface Props {
    update?: boolean;
  }

  let { update = $bindable(false) }: Props = $props();

  let firmwareVersion: string = $state('');
  let firmwareDownloadLink: string = $state('');

  async function getGithubAPI() {
    const headers = {
      accept: 'application/vnd.github+json',
      'X-GitHub-Api-Version': '2022-11-28',
    };
    const result = await api.get<GithubRelease>(
      `https://api.github.com/repos/${page.data.github}/releases/latest`,
      { headers }
    );
    if (result.inner.message === '404' || result.inner.message == 'Not Found') {
      console.warn('Error: Could not find releases in the repository');
      return;
    }
    if (result.isErr()) {
      console.error('Error:', result.inner);
      return;
    }

    const results = result.inner;
    update = false;
    firmwareVersion = '';

    if (compareVersions(results.tag_name, $features.firmware_version as string) === 1) {
      // iterate over assets and find the correct one
      for (let i = 0; i < results.assets.length; i++) {
        // check if the asset is of type *.bin
        if (
          results.assets[i].name.includes('.bin') &&
          results.assets[i].name.includes($features.firmware_built_target as string)
        ) {
          update = true;
          firmwareVersion = results.tag_name;
          firmwareDownloadLink = results.assets[i].browser_download_url;
          notifications.info('Firmware update available.', 5000);
        }
      }
    }
  }

  async function postGithubDownload(url: string) {
    const result = await api.post('/api/downloadUpdate', { download_url: url });
    if (result.isErr()) {
      console.error('Error:', result.inner);
      return;
    }
  }

  onMount(async () => {
    if ($features.download_firmware) {
      await getGithubAPI();
      setInterval(async () => await getGithubAPI(), 60 * 60 * 1000); // once per hour
    }
  });

  function confirmGithubUpdate(url: string) {
    modals.open(ConfirmDialog, {
      title: 'Confirm flashing new firmware to the device',
      message: 'Are you sure you want to overwrite the existing firmware with a new one?',
      labels: {
        cancel: { label: 'Abort', icon: Cancel },
        confirm: { label: 'Update', icon: CloudDown },
      },
      onConfirm: () => {
        postGithubDownload(url);
        modals.open(GithubUpdateDialog, {
          onConfirm: () => modals.closeAll(),
        });
      },
    });
  }
</script>

{#if update}
  <div class="indicator flex-none">
    <button
      class="btn btn-square btn-ghost h-9 w-9"
      onclick={() => confirmGithubUpdate(firmwareDownloadLink)}>
      <span
        class="indicator-item indicator-top indicator-center badge badge-info badge-xs top-2 scale-75 lg:top-1">
        {firmwareVersion}
      </span>
      <Firmware class="h-7 w-7" />
    </button>
  </div>
{/if}
