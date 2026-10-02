# Robot identity and robot list - design

Date: 2026-09-29.
Status: approved in conversation ("Then do it"), option 1 of three.

## Goal

The app keeps one list of the user's robots (two Spot Pico, reporting `SPOTMICRO_ESP32_MINI`, and one Yertle derivative), remembered across sessions.
Each entry shows the robot's name, its type and whether it is online, and connects with one click, or automatically for the last robot used.
The same robot reached by IP and by `.local` name is one entry.

Out of scope: Bluetooth (Leika's firmware has no BLE service; revisit later with the Hexapod NimBLE transport as reference), and the `esp-robot-components` repository.

## Findings that shape the design

- The firmware sends no per-board identifier; `FeaturesDataResponse` carries variant, firmware name, version and target only.
- `#{unique_id}` and `#{platform}` placeholders in `wifi_settings.h` and `ap_settings.h` are never expanded (leftovers from ESP32-SvelteKit).
- The effective mDNS hostname is `APP_NAME` (`main.cpp` `serviceLoopEntry`), so every robot answers as `spot-micro.local`; `WiFiService::setupMDNS` is never called.
- `useFeatureFlags` requests features once per page load, so switching robots keeps the previous robot's flags.
- The app stores robots as `{ address, name, lastSeenAt }` keyed by address (`app/src/lib/stores/robots.ts`).

## Decisions

1. Identity is the factory base MAC from `esp_efuse_mac_get_default`, formatted as 12 uppercase hex digits (`device_id`).
2. The display name lives on the robot, persisted to flash, so every browser shows the same name.
3. Identity and name travel in `FeaturesDataResponse`; renaming is a socket correlation request, not REST, so it works wherever the socket works (robot-served app, hosted app in Chromium, later BLE).
4. Default hostname and AP SSID become unique per robot through the ported Hexapod placeholder expansion.

## Wire protocol (`platform_shared/message.proto`)

- `FeaturesDataResponse` gains `string device_id = 140` and `string robot_name = 150` (both `FT_POINTER`, like the existing strings).
- New `RobotSettings { string name = 1; }` (`max_size:33`) is the persisted settings message.
- New `RobotNameUpdate { string name = 1; }` (`max_size:33`) is `CorrelationRequest.robot_name_update = 130`.
- The reply to `robot_name_update` is `CorrelationResponse.features_data_response`, so the app receives the robot's identity as it now stands.
  An invalid name answers with `status_code = 400` and the unchanged identity.

## Firmware

- `include/settings/placeholders.h`, ported from Hexapod: `substitutePlaceholders` and `toHostLabel`, with `#{unique_id}` defined as the last six hex digits of `device_id`.
- `include/device_identity.h`: `deviceId()` returns the cached 12-digit id; the formatting of six MAC bytes is a pure function.
- `factory_settings.ini`: `FACTORY_WIFI_HOSTNAME="spot-micro-#{unique_id}"`, `FACTORY_AP_SSID="Spot-Micro-#{unique_id}"`, new `FACTORY_ROBOT_NAME="Spot Micro #{unique_id}"`.
- `wifi_settings.h` and `ap_settings.h` defaults run through the placeholder functions (the hostname through `toHostLabel`).
- `RobotService` (`include/robot_service.h`, `src/robot_service.cpp`) is a `StatefulService<socket_message_RobotSettings>` persisted by `FSPersistencePB` to `ROBOT_SETTINGS_FILE` (`/config/robotSettings.pb`).
  `setName` accepts 1 to 32 bytes of UTF-8 after trimming surrounding spaces, rejects control characters, and returns whether the name was valid.
- `main.cpp`: `robotService.begin()` before the server starts; a `robot_name_update` correlation handler; the features handler fills `device_id` and `robot_name`.
- mDNS: `mdns_hostname_set` uses the WiFi hostname instead of `APP_NAME`, and the instance name is the robot name, refreshed on rename.
  The dead `WiFiService::setupMDNS` is deleted.

Existing robots keep their persisted hostname (`spot-micro`) until a factory reset or a hostname edit on the WiFi page; the new defaults apply only to fresh settings.
The mDNS name changes immediately, because it now follows the WiFi hostname rather than `APP_NAME`; robots that still carry `spot-micro` stay ambiguous by `.local` until renamed, but are told apart by `device_id` once connected.

## App

- `robots.ts` stores `Robot { id: string | null, name, variant: string | null, addresses: string[], lastAddress, lastSeenAt }`.
  `id` is `null` for an entry added by address and not yet connected.
  Entries saved in the old shape are migrated on load.
- `identify(address, { deviceId, name, variant })` merges: the address joins the robot with that id (created if new), becomes its `lastAddress`, and any other entry holding only that address is removed.
- Feature flags are re-requested on every socket `open`, and each response identifies the robot at the current `apiLocation`.
- The home page lists robots by name with type and addresses, probes every address of each robot, connects to the address that answered (falling back to `lastAddress`), and forgets by robot.
  Discovery results already belonging to a saved robot are not offered again.
- The connected card offers renaming, which sends `robot_name_update` and applies the reply through `identify`.

## Error handling

- A robot without the new fields (older firmware) sends an empty `device_id`; the app then keeps the entry unidentified, as today.
- A rejected rename shows the firmware's status and keeps the old name.
- A failed feature request leaves the list unchanged.

## Testing

- App (Vitest): migration of old entries, merging by id across two addresses, unidentified-entry replacement, older firmware without `device_id`, rename round trip against a stubbed socket.
- Firmware: the repository has no unit-test harness (`esp32/test` is absent), so the pure functions (MAC formatting, placeholder expansion, host-label sanitising, name validation) are kept free of ESP-IDF headers and checked with a host compiler; the full firmware is verified by a PlatformIO build of the default environment.
- On hardware, not verifiable here: unique mDNS names after a factory reset, and names surviving a reboot.
