<script lang="ts">
	import { goto } from '$app/navigation';
	import { openModal, closeModal } from 'svelte-modals';
	import { slide } from 'svelte/transition';
	import { cubicOut } from 'svelte/easing';
	import { user } from '$lib/stores/user';
	import { notifications } from '$lib/components/toasts/notifications';
	import { PasswordInput } from '$lib/components/input';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import ConfirmDialog from '$lib/components/ConfirmDialog.svelte';
	import EditUser from './EditUser.svelte';
	import Spinner from '$lib/components/Spinner.svelte';

	import { api } from '$lib/api';
	import {
		Cancel,
		Check,
		Users,
		AddUser,
		Admin,
		Edit,
		Delete,
		Warning
	} from '$lib/components/icons';

	type userSetting = {
		username: string;
		password: string;
		admin: boolean;
	};

	type SecuritySettings = {
		jwt_secret: string;
		users: userSetting[];
	};

	let securitySettings: SecuritySettings;

	async function getSecuritySettings() {
		const result = await api.get<SecuritySettings>('/api/securitySettings');
		if (result.isErr()) {
			console.error('Error:', result.inner);
			return;
		}
		securitySettings = result.inner;
	}

	async function postSecuritySettings(data: SecuritySettings) {
		const result = await api.post<SecuritySettings>('/api/securitySettings', data);
		if (result.isErr()) {
			console.error('Error:', result.inner);
			notifications.error('User not authorized.', 3000);
			return;
		}
		securitySettings = result.inner;
		if (await validateUser()) {
			notifications.success('Security settings updated.', 3000);
		}
	}

	async function validateUser() {
		const result = await api.get('/api/verifyAuthorization');
		if (result.isErr()) user.invalidate();
		return result.isOk();
	}

	function confirmDelete(index: number) {
		openModal(ConfirmDialog, {
			title: 'Confirm Delete User',
			message:
				'Are you sure you want to delete the user "' +
				securitySettings.users[index].username +
				'"?',
			labels: {
				cancel: { label: 'Abort', icon: Cancel },
				confirm: { label: 'Yes', icon: Check }
			},
			onConfirm: () => {
				securitySettings.users.splice(index, 1);
				securitySettings = securitySettings;
				closeModal();
				postSecuritySettings(securitySettings);
			}
		});
	}

	function handleEdit(index: number) {
		openModal(EditUser, {
			title: 'Edit User',
			user: { ...securitySettings.users[index] }, // Shallow Copy
			onSaveUser: (editedUser: userSetting) => {
				securitySettings.users[index] = editedUser;
				closeModal();
				postSecuritySettings(securitySettings);
			}
		});
	}

	function handleNewUser() {
		openModal(EditUser, {
			title: 'Add User',
			onSaveUser: (newUser: userSetting) => {
				securitySettings.users = [...securitySettings.users, newUser];
				closeModal();
				postSecuritySettings(securitySettings);
			}
		});
		//
	}
</script>

{#if $user.admin}
	<div
		class="mx-0 my-1 flex flex-col space-y-4
     sm:mx-8 sm:my-8"
	>
		<SettingsCard collapsible={false}>
			<Users slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
			<span slot="title">Manage Users</span>
			{#await getSecuritySettings()}
				<Spinner />
			{:then nothing}
				<div class="relative w-full overflow-visible">
					<button
						class="btn btn-primary text-primary-content btn-md absolute -top-14 right-0"
						on:click={handleNewUser}
					>
						<AddUser class="h-6 w-6" /></button
					>

					<div class="overflow-x-auto" transition:slide|local={{ duration: 300, easing: cubicOut }}>
						<table class="table w-full table-auto">
							<thead>
								<tr class="font-bold">
									<th align="left">Username</th>
									<th align="center">Admin</th>
									<th align="right" class="pr-8">Edit</th>
								</tr>
							</thead>
							<tbody>
								{#each securitySettings.users as user, index}
									<tr>
										<td align="left">{user.username}</td>
										<td align="center">
											{#if user.admin}
												<Admin class="text-secondary" />
											{/if}
										</td>
										<td align="right">
											<span class="my-auto inline-flex flex-row space-x-2">
												<button
													class="btn btn-ghost btn-circle btn-xs"
													on:click={() => handleEdit(index)}
												>
													<Edit class="h-6 w-6" /></button
												>
												<button
													class="btn btn-ghost btn-circle btn-xs"
													on:click={() => confirmDelete(index)}
												>
													<Delete class="text-error h-6 w-6" />
												</button>
											</span>
										</td>
									</tr>
								{/each}
							</tbody>
						</table>
					</div>
				</div>
				<div class="divider mb-0" />

				<span class="pb-2 text-xl font-medium">Security Settings</span>
				<div class="alert alert-warning shadow-lg">
					<Warning class="h-6 w-6 flex-shrink-0" />
					<span
						>The JWT secret is used to sign authentication tokens. If you modify the JWT Secret, all
						users will be signed out.</span
					>
				</div>
				<label class="label" for="secret">
					<span class="label-text text-md">JWT Secret</span>
				</label>
				<PasswordInput bind:value={securitySettings.jwt_secret} id="secret" />
				<div class="mt-6 flex justify-end">
					<button class="btn btn-primary" on:click={() => postSecuritySettings(securitySettings)}
						>Apply Settings</button
					>
				</div>
			{/await}
		</SettingsCard>
	</div>
{:else}
	{goto('/')}
{/if}
