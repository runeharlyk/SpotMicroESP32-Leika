<script lang="ts">
  import { page } from '$app/state'
  import { base } from '$app/paths'
  import { useFeatureFlags } from '$lib/stores/featureFlags'
  import GithubButton from '../menu/GithubButton.svelte'
  import LogoButton from '../menu/LogoButton.svelte'
  import MenuList from '../menu/MenuList.svelte'
  import {
    Connection,
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
    Copyright,
    Metrics,
    DNS
  } from '$lib/components/icons'
  import { PUBLIC_VITE_USE_HOST_NAME } from '$env/static/public'

  const features = useFeatureFlags()

  const appName = page.data.app_name

  const copyright = page.data.copyright

  const github = { href: 'https://github.com/' + page.data.github, active: true }

  type menuItem = {
    title: string
    icon: ConstructorOfATypedSvelteComponent
    href?: string
    feature: boolean
    active?: boolean
    submenu?: menuItem[]
  }

  function withBase(path: string) {
    return `${base}${path.startsWith('/') ? path : '/' + path}`
  }

  let menuItems = $state<menuItem[]>([])

  $effect(() => {
    menuItems = [
      {
        title: 'Connection',
        icon: WiFi,
        href: withBase('/connection'),
        feature: !PUBLIC_VITE_USE_HOST_NAME
      },
      {
        title: 'Controller',
        icon: MdiController,
        href: withBase('/controller'),
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
            href: withBase('/peripherals/i2c'),
            feature: true
          },
          {
            title: 'Camera',
            icon: Camera,
            href: withBase('/peripherals/camera'),
            feature: $features.camera
          },
          {
            title: 'Servo',
            icon: MotorOutline,
            href: withBase('/peripherals/servo'),
            feature: true
          },
          {
            title: 'IMU',
            icon: Rotate3d,
            href: withBase('/peripherals/imu'),
            feature: $features.imu || $features.mag || $features.bmp
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
            href: withBase('/wifi/sta'),
            feature: true
          },
          {
            title: 'Access Point',
            icon: AP,
            href: withBase('/wifi/ap'),
            feature: true
          },
          {
            title: 'mDNS',
            icon: DNS,
            href: withBase('/wifi/mdns'),
            feature: true
          }
        ]
      },
      {
        title: 'System',
        icon: Settings,
        feature: true,
        submenu: [
          {
            title: 'System Status',
            icon: Health,
            href: withBase('/system/status'),
            feature: true
          },
          {
            title: 'File System',
            icon: Folder,
            href: withBase('/system/filesystem'),
            feature: true
          },
          {
            title: 'System Metrics',
            icon: Metrics,
            href: withBase('/system/metrics'),
            feature: true
          },
          {
            title: 'Firmware Update',
            icon: Update,
            href: withBase('/system/update'),
            feature: $features.ota || $features.upload_firmware || $features.download_firmware
          }
        ]
      }
    ] as menuItem[]
  })

  const { menuClicked } = $props()

  function setActiveMenuItem(targetTitle: string) {
    menuItems.forEach(item => {
      item.active = item.title === targetTitle
      item.submenu?.forEach(subItem => {
        subItem.active = subItem.title === targetTitle
      })
    })
    menuItems = menuItems
    menuClicked()
  }

  $effect(() => {
    setActiveMenuItem(page.data.title)
  })

  const updateMenu = (event: any) => {
    setActiveMenuItem(event.details)
  }
</script>

<div class="flex h-full w-80 flex-col p-4 bg-base-200 text-base-content">
  <LogoButton {appName} />

  <MenuList
    {menuItems}
    select={updateMenu}
    class="grow flex-nowrap overflow-y-auto overflow-x-hidden"
    level="0" />

  <div class="divider my-0"></div>

  <div class="flex items-center justify-between">
    <GithubButton {github} />
    <div class="flex items-center justify-end text-sm gap-2">
      <Copyright class="h-4 w-4" />{copyright}
    </div>
  </div>
</div>
