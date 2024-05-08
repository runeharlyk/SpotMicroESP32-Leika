<script lang="ts">
	import { slide } from 'svelte/transition';
	import { cubicOut } from 'svelte/easing';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import Spinner from '$lib/components/Spinner.svelte';
	import MQTT from '~icons/tabler/topology-star-3';
	import Info from '~icons/tabler/info-circle';
	import type { BrokerSettings } from '$lib/types/models';
	import { api } from '$lib/api';

	let brokerSettings: BrokerSettings;

	let formField: any;

	async function getBrokerSettings() {
        const result = await api.get<BrokerSettings>('/api/brokerSettings');
        if (result.isErr()){
            console.error('Error:', result.inner);
            return
        }
        brokerSettings = result.inner
	}

	let formErrors = {
		uid: false,
		path: false,
		name: false
	};

	async function postBrokerSettings() {
        const result = await api.post<BrokerSettings>('/api/brokerSettings', brokerSettings);
        if (result.isErr()){
            console.error('Error:', result.inner);
            notifications.error('User not authorized.', 3000);
            return
        }
        notifications.success('Broker settings updated.', 3000);
        brokerSettings = result.inner
	}

	function handleSubmitBroker() {
		let valid = true;

		// Validate unique ID
		if (brokerSettings.unique_id.length < 3 || brokerSettings.unique_id.length > 32) {
			valid = false;
			formErrors.uid = true;
		} else {
			formErrors.uid = false;
		}

		// Validate name
		if (brokerSettings.name.length < 3 || brokerSettings.name.length > 32) {
			valid = false;
			formErrors.name = true;
		} else {
			formErrors.name = false;
		}
		// Validate MQTT Path
		if (brokerSettings.mqtt_path.length > 64) {
			valid = false;
			formErrors.path = true;
		} else {
			formErrors.path = false;
		}

		// Submit JSON to REST API
		if (valid) {
			postBrokerSettings();
			//alert('Form Valid');
		}
	}
</script>

<SettingsCard collapsible={true} open={false}>
	<MQTT slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
	<span slot="title">MQTT Broker Settings</span>
	<div class="w-full overflow-x-auto">
		{#await getBrokerSettings()}
			<Spinner />
		{:then nothing}
			<form
				on:submit|preventDefault={handleSubmitBroker}
				novalidate
				bind:this={formField}
				transition:slide|local={{ duration: 300, easing: cubicOut }}
			>
				<div class="alert alert-info my-2 shadow-lg">
					<Info class="h-6 w-6 flex-shrink-0 stroke-current" />
					<span
						>The LED is controllable via MQTT with the demo project designed to work with Home
						Assistant's auto discovery feature.</span
					>
				</div>
				<div class="grid w-full grid-cols-1 content-center gap-x-4 px-4">
					<div>
						<label class="label" for="uid">
							<span class="label-text text-md">Unique ID</span>
						</label>
						<input
							type="text"
							class="input input-bordered invalid:border-error w-full invalid:border-2 {formErrors.uid
								? 'border-error border-2'
								: ''}"
							bind:value={brokerSettings.unique_id}
							id="uid"
							min="3"
							max="32"
							required
						/>
						<label class="label" for="uid">
							<span class="label-text-alt text-error {formErrors.uid ? '' : 'hidden'}"
								>Unique ID must be between 3 and 32 characters long</span
							>
						</label>
					</div>
					<div>
						<label class="label" for="name">
							<span class="label-text text-md">Name</span>
						</label>
						<input
							type="text"
							class="input input-bordered invalid:border-error w-full invalid:border-2 {formErrors.name
								? 'border-error border-2'
								: ''}"
							bind:value={brokerSettings.name}
							id="name"
							min="3"
							max="32"
							required
						/>
						<label class="label" for="name">
							<span class="label-text-alt text-error {formErrors.name ? '' : 'hidden'}"
								>Name must be between 3 and 32 characters long</span
							>
						</label>
					</div>
					<div>
						<label class="label" for="path">
							<span class="label-text text-md">MQTT Path</span>
						</label>
						<input
							type="text"
							class="input input-bordered invalid:border-error w-full invalid:border-2 {formErrors.path
								? 'border-error border-2'
								: ''}"
							bind:value={brokerSettings.mqtt_path}
							id="path"
							min="0"
							max="64"
							required
						/>
						<label class="label" for="path">
							<span class="label-text-alt text-error {formErrors.path ? '' : 'hidden'}"
								>MQTT path is limited to 64 characters</span
							>
						</label>
					</div>
				</div>
				<div class="divider mb-2 mt-0" />
				<div class="mx-4 flex flex-wrap justify-end gap-2">
					<button class="btn btn-primary" type="submit">Apply Settings</button>
				</div>
			</form>
		{/await}
	</div>
</SettingsCard>
