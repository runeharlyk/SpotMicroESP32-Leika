<script lang="ts">
  import { page } from '$app/state';
  import { modals } from 'svelte-modals';
  import { slide } from 'svelte/transition';
  import { cubicOut } from 'svelte/easing';
  import ConfirmDialog from '$lib/components/ConfirmDialog.svelte';
  import Spinner from '$lib/components/Spinner.svelte';
  import SettingsCard from '$lib/components/SettingsCard.svelte';

  import { compareVersions } from 'compare-versions';
  import GithubUpdateDialog from '$lib/components/GithubUpdateDialog.svelte';
  import InfoDialog from '$lib/components/InfoDialog.svelte';
  import { api } from '$lib/api';
  import { useFeatureFlags } from '$lib/stores';
  import { Error, Cancel, Check, CloudDown, Github, Prerelease } from '$lib/components/icons';

  const features = useFeatureFlags();

  async function getGithubAPI() {
    const headers = {
      accept: 'application/vnd.github+json',
      'X-GitHub-Api-Version': '2022-11-28',
    };
    const result = await api.get(`https://api.github.com/repos/${page.data.github}/releases`, {
      headers,
    });
    if (result.isErr()) {
      console.error('Error:', result.inner);
      return;
    }
    return result.inner as any;
  }

  async function postGithubDownload(url: string) {
    const result = await api.post('/api/firmware/download', { download_url: url });
    if (result.isErr()) {
      console.error('Error:', result.inner);
      return;
    }
  }

  function confirmGithubUpdate(assets: any) {
    let url = '';
    // iterate over assets and find the correct one
    for (let i = 0; i < assets.length; i++) {
      // check if the asset is of type *.bin
      if (
        assets[i].name.includes('.bin') &&
        assets[i].name.includes($features.firmware_built_target)
      ) {
        url = assets[i].browser_download_url;
      }
    }
    if (url === '') {
      // if no asset was found, use the first one
      modals.open(InfoDialog, {
        title: 'No matching firmware found',
        message:
          'No matching firmware was found for the current device. Upload the firmware manually or build from sources.',
        dismiss: { label: 'OK', icon: Check },
        onDismiss: () => modals.close(),
      });
      return;
    }

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

<SettingsCard collapsible={false}>
  {#snippet icon()}
    <Github class="lex-shrink-0 mr-2 h-6 w-6 self-end rounded-full" />
  {/snippet}
  {#snippet title()}
    <span>Github Firmware Manager</span>
  {/snippet}
  {#await getGithubAPI()}
    <Spinner />
  {:then githubReleases}
    <div class="relative w-full overflow-visible">
      <div class="overflow-x-auto" transition:slide|local={{ duration: 300, easing: cubicOut }}>
        <table class="table w-full table-auto">
          <thead>
            <tr class="font-bold">
              <th align="left">Release</th>
              <th align="center" class="hidden sm:block">Release Date</th>
              <th align="center">Experimental</th>
              <th align="center">Install</th>
            </tr>
          </thead>
          <tbody>
            {#each githubReleases as release}
              <tr
                class={(
                  compareVersions($features.firmware_version as string, release.tag_name) === 0
                ) ?
                  'bg-primary text-primary-content'
                : 'bg-base-100 h-14'}>
                <td align="left" class="text-base font-semibold">
                  <a
                    href={release.html_url}
                    class="link link-hover"
                    target="_blank"
                    rel="noopener noreferrer">{release.name}</a
                  ></td>
                <td align="center" class="hidden min-h-full align-middle sm:block">
                  <div class="my-2">
                    {new Intl.DateTimeFormat('en-GB', {
                      dateStyle: 'medium',
                    }).format(new Date(release.published_at))}
                  </div>
                </td>
                <td align="center">
                  {#if release.prerelease}
                    <Prerelease class="text-accent h-5 w-5" />
                  {/if}
                </td>
                <td align="center">
                  {#if compareVersions($features.firmware_version as string, release.tag_name) != 0}
                    <button
                      class="btn btn-ghost btn-circle btn-sm"
                      onclick={() => {
                        confirmGithubUpdate(release.assets);
                      }}>
                      <CloudDown class="text-secondary h-6 w-6" />
                    </button>
                  {/if}
                </td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
    </div>
  {:catch error}
    <div class="alert alert-error shadow-lg">
      <Error class="h-6 w-6 shrink-0" />
      <span>Please connect to a network with internet access to perform a firmware update.</span>
    </div>
  {/await}
</SettingsCard>
