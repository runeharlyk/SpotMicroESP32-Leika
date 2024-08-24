<script lang="ts">
	import { page } from '$app/stores';
	import { user } from '$lib/stores/user';
	import { createEventDispatcher } from 'svelte';
	import { useFeatureFlags } from '$lib/stores/featureFlags';
	import UserButton from '../menu/UserButton.svelte';
	import GithubButton from '../menu/GithubButton.svelte';
	import LogoButton from '../menu/LogoButton.svelte';
	import MenuList from '../menu/MenuList.svelte';
	import {
		Connection,
		Users,
		Settings,
		MdiController,
		Devices,
		Camera,
		Rotate3d,
		MotorOutline,
		Health,
		Folder,
		Update,
		WiFi,
		Router,
		AP,
		Remote,
		Copyright,
		NTP,
		Metrics
	} from '$lib/components/icons';
	import appEnv from 'app-env';

	const features = useFeatureFlags();

	const appName = $page.data.app_name;

	const copyright = $page.data.copyright;

	const github = { href: 'https://github.com/' + $page.data.github, active: true };

	type menuItem = {
		title: string;
		icon: ConstructorOfATypedSvelteComponent;
		href?: string;
		feature: boolean;
		active?: boolean;
		submenu?: menuItem[];
	};

	$: menuItems = [
		{
			title: 'Connection',
			icon: WiFi,
			href: '/connection',
			feature: !appEnv.VITE_USE_HOST_NAME
		},
		{
			title: 'Controller',
			icon: MdiController,
			href: '/controller',
			feature: true
		},
		{
			title: 'Peripherals',
			icon: Devices,
			feature: true,
			submenu: [
				{
					title: 'I2C',
					icon: Connection,
					href: '/peripherals/i2c',
					feature: true
				},
				{
					title: 'Camera',
					icon: Camera,
					href: '/peripherals/camera',
					feature: $features.camera
				},
				{
					title: 'Servo',
					icon: MotorOutline,
					href: '/peripherals/servo',
					feature: true
				},
				{
					title: 'IMU',
					icon: Rotate3d,
					href: '/peripherals/imu',
					feature: $features.imu || $features.mag || $features.bmp
				}
			]
		},
		{
			title: 'Connections',
			icon: Remote,
			feature: $features.ntp,
			submenu: [
				{
					title: 'NTP',
					icon: NTP,
					href: '/connections/ntp',
					feature: $features.ntp
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
					feature: true
				},
				{
					title: 'Access Point',
					icon: AP,
					href: '/wifi/ap',
					feature: true
				}
			]
		},
		{
			title: 'Users',
			icon: Users,
			href: '/user',
			feature: $features.security && $user.admin
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
					feature: true
				},
				{
					title: 'File System',
					icon: Folder,
					href: '/system/filesystem',
					feature: true
				},
				{
					title: 'System Metrics',
					icon: Metrics,
					href: '/system/metrics',
					feature: $features.analytics
				},
				{
					title: 'Firmware Update',
					icon: Update,
					href: '/system/update',
					feature:
						($features.ota || $features.upload_firmware || $features.download_firmware) &&
						(!$features.security || $user.admin)
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
		menuItems = menuItems;
		dispatch('menuClicked');
	}

	$: setActiveMenuItem($page.data.title);

	const updateMenu = (event: any) => {
		setActiveMenuItem(event.details);
	};
</script>

<div class="bg-base-200 text-base-content flex h-full w-80 flex-col p-4">
	<LogoButton {appName} />

	<MenuList {menuItems} on:select{updateMenu} class="flex-grow flex-nowrap overflow-y-auto" />

	<UserButton />

	<div class="divider my-0" />

	<div class="flex items-center justify-between">
		<GithubButton {github} />
		<div class="flex items-center justify-end text-sm gap-2">
			<Copyright class="h-4 w-4" />{copyright}
		</div>
	</div>
</div>
