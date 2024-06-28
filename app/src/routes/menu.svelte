<script lang="ts">
	import logo from '$lib/assets/logo512.png';
	import MdiGithub from '~icons/mdi/github';
	import Users from '~icons/mdi/users';
	import Settings from '~icons/mdi/settings';
    import MdiController from '~icons/mdi/controller';
    import Devices from '~icons/mdi/devices'
    import Camera from '~icons/mdi/camera-outline';
    import Rotate3d from '~icons/mdi/rotate-3d';
    import MotorOutline from '~icons/mdi/motor-outline';
	import Health from '~icons/mdi/stethoscope';
	import Folder from '~icons/mdi/folder-outline';
	import Update from '~icons/mdi/reload';
	import WiFi from '~icons/mdi/wifi';
	import Router from '~icons/mdi/router';
	import AP from '~icons/mdi/access-point';
	import Remote from '~icons/mdi/network';
	import Avatar from '~icons/mdi/user-circle';
	import Logout from '~icons/mdi/logout';
	import Copyright from '~icons/mdi/copyright';
	import NTP from '~icons/mdi/clock-check';
	import Metrics from '~icons/mdi/report-bar';
	import { page } from '$app/stores';
	import { user } from '$lib/stores/user';
	import { createEventDispatcher } from 'svelte';

	const appName = $page.data.app_name;

	const copyright = $page.data.copyright;

	const github = { href: 'https://github.com/' + $page.data.github, active: true };

	type menuItem = {
		title: string;
		icon: ConstructorOfATypedSvelteComponent;
		href?: string;
		feature: boolean;
		active?: boolean;
		submenu?: subMenuItem[];
	};

	type subMenuItem = {
		title: string;
		icon: ConstructorOfATypedSvelteComponent;
		href: string;
		feature: boolean;
		active: boolean;
	};

	let menuItems = [
        {
            title: 'Controller',
            icon: MdiController,
            href: '/controller',
            feature: true,
        },
        {
			title: 'Peripherals',
			icon: Devices,
			feature: true,
			submenu: [
				{
					title: 'Camera',
					icon: Camera,
					href: '/peripherals/camera',
					feature: $page.data.features.camera,
				},
                {
					title: 'Servo',
					icon: MotorOutline,
					href: '/peripherals/servo',
					feature: true,
				},
                {
					title: 'IMU',
					icon: Rotate3d,
					href: '/peripherals/imu',
					feature: $page.data.features.imu || $page.data.features.mag || $page.data.features.bmp,
				}
			]
		},
		{
			title: 'Connections',
			icon: Remote,
			feature: $page.data.features.ntp,
			submenu: [
				{
					title: 'NTP',
					icon: NTP,
					href: '/connections/ntp',
					feature: $page.data.features.ntp,
					
				}
			]
		},
		{
			title: 'WiFi',
			icon: WiFi,
			feature: true,
			submenu: [
				{
					title: 'WiFi Station',
					icon: Router,
					href: '/wifi/sta',
					feature: true,
					
				},
				{
					title: 'Access Point',
					icon: AP,
					href: '/wifi/ap',
					feature: true,
					
				}
			]
		},
		{
			title: 'Users',
			icon: Users,
			href: '/user',
			feature: $page.data.features.security && $user.admin,
			
		},
		{
			title: 'System',
			icon: Settings,
			feature: true,
			submenu: [
				{
					title: 'System Status',
					icon: Health,
					href: '/system/status',
					feature: true,
					
				},
                {
					title: 'File System',
					icon: Folder,
					href: '/system/filesystem',
					feature: true,
					
				},
				{
					title: 'System Metrics',
					icon: Metrics,
					href: '/system/metrics',
					feature: $page.data.features.analytics,
					
				},
				{
					title: 'Firmware Update',
					icon: Update,
					href: '/system/update',
					feature:
						($page.data.features.ota ||
							$page.data.features.upload_firmware ||
							$page.data.features.download_firmware) &&
						(!$page.data.features.security || $user.admin),
				}
			]
		}
	] as menuItem[];

	const dispatch = createEventDispatcher();

	function setActiveMenuItem(targetTitle: string) {
		menuItems.forEach(item => {
			item.active = item.title === targetTitle;
			item.submenu?.forEach(subItem => {
				subItem.active = subItem.title === targetTitle;
			});
		});
		menuItems = menuItems
		dispatch('menuClicked');
	}

	$: setActiveMenuItem($page.data.title);
</script>

<div class="bg-base-200 text-base-content flex h-full w-80 flex-col p-4">
	<!-- Sidebar content here -->
	<a
		href="/"
		class="rounded-box mb-4 flex items-center hover:scale-[1.02] active:scale-[0.98]"
		on:click={() => setActiveMenuItem('')}
	>
		<img src={logo} alt="Logo" class="h-12 w-12" />
		<h1 class="px-4 text-2xl font-bold">{appName}</h1>
	</a>
	<ul class="menu rounded-box menu-vertical flex-nowrap overflow-y-auto">
		{#each menuItems as menuItem, i (menuItem.title)}
			{#if menuItem.feature}
				<li>
					{#if menuItem.submenu}
						<details>
							<summary class="text-lg font-bold">
								<svelte:component this={menuItem.icon} class="h-6 w-6" />
								{menuItem.title}
							</summary>
							<ul>
								{#each menuItem.submenu as subMenuItem}
									{#if subMenuItem.feature}
										<li class="hover-bordered">
											<a
												href={subMenuItem.href}
												class:bg-base-100={subMenuItem.active}
												class="text-ml font-bold"
												on:click={() => {
													setActiveMenuItem(subMenuItem.title);
													menuItems = menuItems;
												}}
												><svelte:component
													this={subMenuItem.icon}
													class="h-5 w-5"
												/>{subMenuItem.title}</a
											>
										</li>
									{/if}
								{/each}
							</ul>
						</details>
					{:else}
						<a
							href={menuItem.href}
							class:bg-base-100={menuItem.active}
							class="text-lg font-bold"
							on:click={() => {
								setActiveMenuItem(menuItem.title);
								menuItems = menuItems;
							}}><svelte:component this={menuItem.icon} class="h-6 w-6" />{menuItem.title}</a
						>
					{/if}
				</li>
			{/if}
		{/each}
	</ul>

	<div class="flex-col" />
	<div class="flex-grow" />

	{#if $page.data.features.security}
		<div class="flex items-center">
			<Avatar class="h-8 w-8" />
			<span class="flex-grow px-4 text-xl font-bold">{$user.username}</span>
			<!-- svelte-ignore a11y-click-events-have-key-events -->
			<!-- svelte-ignore a11y-no-static-element-interactions -->
			<div
				class="btn btn-ghost"
				on:click={() => {
					user.invalidate();
				}}
			>
				<Logout class="h-8 w-8 rotate-180" />
			</div>
		</div>
	{/if}

	<div class="divider my-0" />
	<div class="flex items-center">
		{#if github.active}
			<a href={github.href} class="btn btn-ghost" target="_blank" rel="noopener noreferrer"
				><MdiGithub class="h-5 w-5" /></a
			>
		{/if}
		<div class="inline-flex flex-grow items-center justify-end text-sm">
			<Copyright class="h-4 w-4" /><span class="px-2">{copyright}</span>
		</div>
	</div>
</div>