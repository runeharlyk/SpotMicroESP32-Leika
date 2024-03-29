<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import { user } from '$lib/stores/user';
    import { servoAngles } from '$lib/stores';
	import { page } from '$app/stores';
	import { notifications } from '$lib/components/toasts/notifications';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import Light from '~icons/tabler/bulb';


	async function getActuatorState() {
		try {
			const response = await fetch('/rest/actuators', {
				method: 'GET',
				headers: {
					Authorization: $page.data.features.security ? 'Bearer ' + $user.bearer_token : 'Basic',
					'Content-Type': 'application/json'
				}
			});
			const actuators = await response.json();
			servoAngles.set(actuators.state);
		} catch (error) {
			console.error('Error:', error);
		}
		return;
	}

	const ws_token = $page.data.features.security ? '?access_token=' + $user.bearer_token : '';

	const socket = new WebSocket('ws://' + $page.url.host + '/ws' + ws_token);

	socket.onopen = (event) => {
		// socket.send('Hello');
	};

	socket.addEventListener('close', (event) => {
		const closeCode = event.code;
		const closeReason = event.reason;
		console.log('WebSocket closed with code:', closeCode);
		console.log('Close reason:', closeReason);
		notifications.error('Websocket disconnected', 5000);
	});

	socket.onmessage = (event) => {
		const message = JSON.parse(event.data);
		if (message.type != 'id') {
			servoAngles.set(message.state);
		}
	};

	onDestroy(() => socket.close());

	onMount(() => getActuatorState());

    function updateAngle(index: number, value: number) {
        servoAngles.update(($servoAngles) => {
        $servoAngles[index] = value;
        socket.send(JSON.stringify({ state: $servoAngles }));
        return $servoAngles;
        });
    }
</script>

<SettingsCard collapsible={false}>
	<Light slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
	<span slot="title">Light State</span>
	<div class="w-full">
		<h1 class="text-xl font-semibold">Websocket Example</h1>
		<div class="form-control">
        <div class="w-full flex justify-between">
        {#each $servoAngles as angle, index}
                <input
                type="number"
                class="input w-12"
                id={`angle-${index}`}
                value={angle}
                on:input={(event) => updateAngle(index, parseFloat(event.target?.value))}
                />
            {/each}
        </div>
		</div>
	</div>
</SettingsCard>
