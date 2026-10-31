# iPhone Webcam

[![Build](https://github.com/liamniou/iphone-webcam/actions/workflows/build.yml/badge.svg)](https://github.com/liamniou/iphone-webcam/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

Use your iPhone as a **4K 30 fps webcam** for Windows 11 over Wi-Fi — a small, open-source alternative to Camo Studio.
The iPhone streams H.264 video to a desktop app that feeds a virtual camera called **iPhone Webcam**, which you can pick in Zoom, Teams, Google Meet, OBS, Discord, the Windows Camera app, …

- 3840×2160, 1920×1080 or 1280×720 at 30 fps (the app using the camera picks the size)
- **Auto-framing**: zooms in and smoothly follows your face (like Center Stage), from wide to tight framing
- **Retouch**: reduce facial redness and smooth skin — applied only to skin on your face
- GPU-accelerated decoding and effects, low latency, automatic discovery and reconnection
- Desktop app with live preview, status, mirror/rotate, system tray (*Close to tray*) and *Start with Windows*
- Builds for **Windows x64** and **Windows ARM64**

## Requirements

- **Windows 11** (x64 or ARM64). The virtual camera API doesn't exist on Windows 10.
- **iPhone** with iOS 16 or later. 4K needs an iPhone 6s or newer (back camera) / iPhone 11 or newer (front camera); otherwise it streams 1080p.
- PC and iPhone on the **same Wi-Fi network** (5 GHz recommended; the PC may be on Ethernet).
- A free **Apple ID** to install the iPhone app.

## Install on Windows

1. Open the [latest release](https://github.com/liamniou/iphone-webcam/releases/latest) and download the setup for your PC:
   - `IPhoneWebcam-Setup-<version>-x64.exe` for Intel/AMD PCs (most PCs)
   - `IPhoneWebcam-Setup-<version>-arm64.exe` for ARM PCs (Snapdragon, e.g. Surface Pro 11, Copilot+ PCs)

   Not sure? *Settings → System → About → System type* says "x64-based" or "ARM-based".
2. Run it. The installer isn't code-signed, so if SmartScreen shows *"Windows protected your PC"*, click **More info → Run anyway**. Approve the administrator prompt (needed to register the virtual camera).
3. Start **iPhone Webcam** from the Start menu.

Nothing else is needed — the .NET runtime is included. To uninstall, use *Settings → Apps → Installed apps → iPhone Webcam*.

<details>
<summary>Install without the setup program (.zip)</summary>

Download `IPhoneWebcam-<version>-<arch>.zip`, extract it, then in an **administrator** PowerShell inside the extracted folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1     # copies to Program Files, registers the camera, adds a Start menu shortcut
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1   # removes it again
```
</details>

## Install on iPhone

Apple only allows App Store apps to be installed freely, so the app is *sideloaded*: you sign it with your own Apple ID.
With a free Apple ID the installation is valid for **7 days**; after that, simply sideload it again (your settings stay).
Choose **one** of these options. *Sideloadly* is quickest for a one-off install; *AltStore* takes a bit longer to set up but renews the app automatically, so you don't have to reinstall it every week.

### Option A — from Windows with Sideloadly (no Mac needed)

1. Download `IPhoneWebcam-<version>-unsigned.ipa` from the [latest release](https://github.com/liamniou/iphone-webcam/releases/latest).
2. Install **iTunes** and **iCloud** for Windows from Apple's direct download links — *not* the Microsoft Store versions (Sideloadly needs the drivers and Apple ID components they install). If you have the Microsoft Store versions, uninstall them first.
   - iTunes (64-bit): <https://www.apple.com/itunes/download/win64>
   - iCloud: <https://updates.cdn-apple.com/2020/windows/001-39935-20200911-1A70AA56-F448-11EA-8CC0-99D41950005E/iCloudSetup.exe>
     (iCloud 7.x — Apple's last standalone installer; newer versions are only in the Microsoft Store. You don't need to sign in to iCloud.)

   Restart the PC after installing both.
3. Install [Sideloadly](https://sideloadly.io/) and start it.
4. Connect the iPhone with a USB cable, unlock it and tap **Trust** on the phone.
5. In Sideloadly: drag the `.ipa` onto the window, select your iPhone, enter your Apple ID and click **Start**. Enter the verification code if asked.
   - If you get an error that the bundle ID isn't available, open *Advanced options* and enable **Change bundle ID** (use e.g. `com.yourname.iphonewebcam`).
6. On the iPhone:
   - enable **Developer Mode**: *Settings → Privacy & Security → Developer Mode* → on, then restart and confirm;
   - trust your certificate: *Settings → General → VPN & Device Management* → your Apple ID → **Trust**.
7. Open **iPhone Webcam** and allow **Camera** and **Local Network** access.

To renew after 7 days, repeat step 5 (or enable Sideloadly's automatic refresh).

### Option B — AltStore (refreshes automatically every week)

[AltStore](https://altstore.io/) is an app store for sideloaded apps. A companion program, **AltServer**, runs on your computer and re-signs your apps over Wi-Fi in the background, so the 7-day limit takes care of itself as long as the PC is on and on the same Wi-Fi from time to time.

> Use **AltStore Classic** (the one installed with AltServer). *AltStore PAL* (EU, Japan, Brazil) only installs notarized apps, so it can't install this `.ipa`.

Limits of a free Apple ID: at most **3 sideloaded apps** on the device (AltStore itself uses one) and **10 App IDs per week** (iPhone Webcam needs one).

<details open>
<summary><b>Step 1 — Install AltServer on your Windows PC</b> (one time)</summary>

1. Install **iTunes** and **iCloud** downloaded **directly from Apple** — *not* the Microsoft Store versions (AltServer needs their device drivers and Apple ID libraries):
   - iTunes (64-bit): <https://www.apple.com/itunes/download/win64>
   - iCloud: <https://updates.cdn-apple.com/2020/windows/001-39935-20200911-1A70AA56-F448-11EA-8CC0-99D41950005E/iCloudSetup.exe>
     (Apple's last standalone installer, as recommended by the [official AltStore guide](https://faq.altstore.io/altstore-classic/how-to-install-altstore-windows))

   If you already have the Microsoft Store versions, uninstall them first (or see AltStore's [troubleshooting guide](https://faq.altstore.io/altstore-classic/troubleshooting-guide) for a workaround).
2. Download [AltServer for Windows](https://cdn.altstore.io/file/altstore/altinstaller.zip), extract `AltInstaller.zip` and run `Setup.exe`.
3. Start AltServer: type **AltServer** in the Start menu search, choose **Run as administrator**. When Windows Firewall asks, **allow access on private networks**. AltServer runs as an icon in the system tray (click the `^` arrow in the taskbar if you don't see it).

On a Mac, use [AltServer for macOS](https://faq.altstore.io/altstore-classic/how-to-install-altstore-macos) instead (it lives in the menu bar; the rest of this guide is the same).
</details>

<details open>
<summary><b>Step 2 — Install AltStore on your iPhone</b> (one time)</summary>

1. Connect the iPhone to the PC with a USB cable, unlock it and tap **Trust** (enter your passcode).
2. Open **iTunes**, sign in with your Apple ID, click the phone icon, and in *Summary* enable **Sync with this iPhone over Wi-Fi**, then click **Apply**. (This lets AltServer reach the phone over Wi-Fi later.)
3. Click the AltServer tray icon → **Install AltStore** → choose your iPhone.
4. Enter your Apple ID email and password. They are sent only to Apple, to create the free signing certificate. (Using a separate, free Apple ID just for sideloading is fine too.)
5. Wait for the notification *"Installation Succeeded"*. AltStore appears on the iPhone's home screen (restart the iPhone if it doesn't).
6. On the iPhone, trust the certificate: **Settings → General → VPN & Device Management** → under *Developer App* tap your Apple ID → **Trust "…"** → **Trust**.
7. Enable **Developer Mode** (iOS 16+): **Settings → Privacy & Security → Developer Mode** → on → **Restart**. After the restart, unlock the phone and tap **Turn On** when asked.
   (The *Developer Mode* switch only appears after the first sideloaded app has been installed — that's why it comes after step 5.)
</details>

<details open>
<summary><b>Step 3 — Download the iPhone Webcam app onto the iPhone</b></summary>

1. On the iPhone, open **Safari** and go to <https://github.com/liamniou/iphone-webcam/releases/latest>.
2. Under *Assets*, tap **`IPhoneWebcam-<version>-unsigned.ipa`** → **Download**. Safari saves it in the **Files** app → *On My iPhone* (or *iCloud Drive*) → **Downloads**.

   *Alternatively, download it on the PC and copy it to the iPhone through iCloud Drive, or via AirDrop from a Mac.*
</details>

<details open>
<summary><b>Step 4 — Install it with AltStore</b></summary>

1. Make sure **AltServer is running on the PC** and both devices are on the **same Wi-Fi** (or connect the iPhone with the USB cable — that always works).
2. Open **AltStore** on the iPhone. If it asks you to sign in, use the same Apple ID as in step 2 (*Settings* tab → *Sign in with Apple ID*).
3. Go to the **My Apps** tab and tap **+** (top-left).
4. Pick `IPhoneWebcam-<version>-unsigned.ipa` from **Downloads**.
5. Wait while AltStore signs and installs it (a progress bar is shown; it registers one App ID for the app — you don't need to change the bundle ID). **iPhone Webcam** appears on the home screen and in *My Apps* with "Expires in 7 days".
6. Open **iPhone Webcam** and tap **Allow** for **Camera** and for **Local Network** — the PC can't find the phone without the Local Network permission. The certificate is the same one you trusted for AltStore, so no extra trust step is needed.
</details>

<details>
<summary><b>Keeping it working (the 7-day limit)</b></summary>

- AltStore **refreshes your apps automatically** in the background when AltServer is running on the PC and the iPhone is on the same Wi-Fi. Keep AltServer running and start it when the PC starts (e.g. put an AltServer shortcut, set to *Run as administrator*, in the Startup folder: press `Win+R`, type `shell:startup`). Keep *Settings → General → Background App Refresh* on for AltStore.
- To refresh manually: open AltStore → **My Apps** → **Refresh All** (with AltServer reachable). Do it any time before the 7 days run out; the counter restarts.
- If the app already expired (it won't open, or iOS says it's *no longer available*): refresh it in AltStore the same way — your settings are kept.
- If **AltStore itself** expired: click the AltServer tray icon → **Install AltStore** again (step 2.3) **without deleting AltStore first**, then tap *Refresh All*.
</details>

<details>
<summary><b>Updating to a new version</b></summary>

Download the new `.ipa` in Safari (step 3), then in AltStore → **My Apps** → **+** pick the new file. It replaces the old version and keeps its settings.
</details>

<details>
<summary><b>AltStore troubleshooting</b></summary>

| Problem | Fix |
|---------|-----|
| *"Could not find AltServer"* | AltServer must be running and on the same Wi-Fi. Allow AltServer through Windows Firewall (private networks), avoid guest/school/work Wi-Fi (it isolates devices), or simply plug the iPhone into the PC with USB and try again. |
| AltServer can't see the iPhone / sign-in hangs | iTunes and iCloud must be the versions from apple.com (not the Microsoft Store) and running; run AltServer **as administrator**; re-plug the cable and tap *Trust* on the iPhone. |
| *"Maximum number of apps"* / 3-app limit | Only 3 sideloaded apps can be active at a time, including AltStore. In *My Apps*, long-press an app → **Deactivate** to free a slot (its data is backed up; tap **ACTIVATE** later to restore it). |
| *"Maximum number of App IDs"* | A free Apple ID can register 10 App IDs per 7 days. Wait until some expire (*My Apps → View App IDs* shows when). |
| *"Untrusted Developer"* when opening an app | Trust the certificate: *Settings → General → VPN & Device Management*. |
| App opens and closes immediately / *"Developer Mode required"* | Turn on *Settings → Privacy & Security → Developer Mode* and restart. |
| The PC never finds the phone in iPhone Webcam | *Settings → Privacy & Security → Local Network* → enable **iPhone Webcam**, or use *Connect by IP address* in the desktop app. |

More help: [official AltStore troubleshooting guide](https://faq.altstore.io/altstore-classic/troubleshooting-guide).
</details>

### Option C — build with Xcode on a Mac

1. Clone the repository on the Mac and generate the project with [XcodeGen](https://github.com/yonaskolb/XcodeGen):
   ```sh
   brew install xcodegen
   cd ios && xcodegen
   open IPhoneWebcam.xcodeproj
   ```
2. Xcode → *Settings → Accounts*: add your Apple ID. In the target's *Signing & Capabilities*, choose your *Personal Team*. If the bundle identifier is reported as unavailable, change it to something unique (e.g. `com.yourname.iphonewebcam`).
3. Connect the iPhone, enable **Developer Mode** on it (see above), select it as the run destination and press **Run**.
4. Trust the certificate on the iPhone (see above) and allow Camera and Local Network access.

## Using it

1. Start **iPhone Webcam** on the PC and open the app on the iPhone. The PC finds the phone automatically and connects; the preview appears in the desktop app.
   To pick another phone, use the **Device** list in the left panel. If the phone isn't found (some routers block discovery), click **Connect by IP address…** and enter the IP shown in the iPhone app.
2. Mount the iPhone in **landscape** (either direction — the image is kept upright). The app must stay open in the foreground; the screen won't auto-lock.
3. In your video app choose the camera **iPhone Webcam**. Apps such as Teams or Zoom usually pick 1080p/720p; in OBS or the Windows Camera app you can select 3840×2160.

The desktop app follows the Windows 11 look (light or dark, matching Windows): the **left panel** holds the device and the effects, the **title bar** shows whether an app is using the camera and the received resolution/frame rate, and the **status panel** on the right (toggle with the panel button in the title bar) shows connection details. The **gear** button opens **Settings**: app theme, *Run at startup*, *Close to tray*, connect by IP, automatic reconnection, GPU decoding and camera status (Ctrl+1…4 switch between its pages).

Closing the window keeps the camera running in the **system tray**; right-click the tray icon to show or hide the window, switch or disconnect the iPhone, open Settings or quit. Turn off **Close to tray** in Settings if you'd rather have the × button quit the app. The camera switch button on the iPhone toggles front/back camera.

### Auto-framing and retouch

The iPhone app detects faces (Apple's Vision framework, a few times per second) and sends their positions to the PC, where the effects are applied on the GPU to the final picture — so they work in every video app.

- **Auto-framing**: the picture zooms in and smoothly pans to keep your face (or all faces) well framed, and zooms back out a couple of seconds after nobody is visible. The slider goes from *Wide* (head and shoulders) to *Tight* (head fills the picture); zoom is limited to 3× so the 4K picture stays sharp at 1080p.
- **Retouch**: **Reduce redness** evens out a red complexion; **Smooth skin** softens skin texture while keeping eyes, hair and edges sharp. Both only touch skin-coloured pixels inside the detected face area, so red clothes or objects stay as they are. (With an older iPhone app version that doesn't send face positions, retouching applies to all skin-coloured pixels.)

Effects need a DirectX 11 graphics card (any PC that runs Windows 11 has one); the line under the *Auto-framing* slider shows whether a face is detected.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| The setup says this PC isn't supported | You downloaded the wrong architecture (x64 vs arm64), or the PC runs Windows 10. |
| Red banner "The virtual camera isn't available" | Reinstall with the setup program (it registers the camera). Windows 11 is required. |
| The phone doesn't appear in the list | Same network? App open on the iPhone? *Settings → Privacy & Security → Local Network → iPhone Webcam* enabled? Guest/public Wi-Fi often isolates devices. Use *Connect by IP address*. |
| Video apps show a gray picture | The desktop app isn't receiving video — check its status panel. |
| Choppy video / freezes | 4K needs ~30 Mbit/s: move closer to the router, use 5/6 GHz Wi-Fi, connect the PC by Ethernet. |
| Decoder shows `CPU (software)` and < 30 fps | GPU decoding isn't available: update the graphics driver. |
| Auto-framing shows "Face detection needs the latest iPhone app" | Install the current `.ipa` on the iPhone. |
| Auto-framing doesn't zoom | Make sure the face is detected (status under *Auto-framing*); a face that already fills much of the picture needs little or no zoom — try *Tight*. |
| The iPhone app stopped opening | The 7-day free signing expired — sideload it again. |

## Build from source

<details>
<summary>Windows (x64 / ARM64)</summary>

Prerequisites: Visual Studio 2022 or Build Tools with the *Desktop development with C++* workload (plus *MSVC ARM64 build tools* for ARM64), CMake, a Windows 11 SDK, the .NET 10 SDK and, for the installer, [Inno Setup 6](https://jrsoftware.org/isinfo.php).

```powershell
cd windows
.\build.cmd x64 1.0.0                              # or: .\build.cmd arm64 1.0.0  →  build\<arch>\app
.\installer\package.ps1 -Arch x64 -Version 1.0.0   # →  build\dist\IPhoneWebcam-Setup-1.0.0-x64.exe and .zip
```

To install a local build directly: `powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1` (elevated).

Testing without an iPhone:

```powershell
.\build\x64\native\FakePhone.exe --width 3840 --height 2160   # moving 4K test pattern with a synthetic face on 127.0.0.1:5005
                                                              # (--face-size 0.15, --no-faces: no face data, --no-face: plain pattern)
# Run the desktop app next to an installed copy, without touching the real virtual camera:
.\build\x64\app\IPhoneWebcam.exe --dev                        # then connect to 127.0.0.1
# or headless, saving the processed picture:
.\build\x64\app\IPhoneWebcamCli.exe --ip 127.0.0.1 --no-camera --autoframe --framing 70 --redness 80 --smooth 60 --snapshot out.bmp
.\build\x64\native\CaptureTest.exe capture.png 90 3840        # opens the virtual camera, prints fps, saves a frame
```
</details>

<details>
<summary>iPhone app</summary>

On a Mac with Xcode: `ios/build-ipa.sh <version> <build>` produces an unsigned `ios/build/IPhoneWebcam-<version>-unsigned.ipa`, or open the XcodeGen project as described in *Option C*.
</details>

<details>
<summary>Releases (CI)</summary>

[GitHub Actions](.github/workflows/build.yml) builds the Windows x64 and ARM64 installers/zips and the unsigned iOS `.ipa` for every push and pull request (downloadable as workflow artifacts). Pushing a tag like `v1.2.3` publishes them as a GitHub Release:

```sh
git tag v1.2.3 && git push origin v1.2.3
```
</details>

## How it works

```
 iPhone app (Swift)                         Windows PC
┌──────────────────────┐   Wi-Fi/TCP   ┌───────────────────────────────┐  shared   ┌────────────────────────────┐
│ AVCapture 4K30       │──────────────▶│ IPhoneWebcam.exe (WPF desktop) │  memory   │ IPhoneWebcamSource.dll     │
│ VideoToolbox H.264   │  H.264 Annex B │  └ IPhoneWebcamEngine.dll      │─────────▶│ Media Foundation virtual   │──▶ Zoom, Teams,
│ Bonjour + TCP :5005  │◀── 'K' (IDR) ──│    mDNS, GPU decode, vcam reg. │   NV12    │ camera (in Frame Server)   │    OBS, Camera…
└──────────────────────┘               └───────────────────────────────┘           └────────────────────────────┘
```

| Path | What |
|------|------|
| [ios/](ios/) | iPhone app (SwiftUI, AVFoundation, VideoToolbox, Network.framework) |
| [windows/desktop/](windows/desktop/) | Desktop app (C# WPF, .NET 10, Fluent design with [WPF UI](https://github.com/lepoco/wpfui)): preview, device and effect controls, status, Settings window, tray |
| [windows/engine/](windows/engine/) | `IPhoneWebcamEngine.dll` (C++): discovery, connection, GPU H.264 decoding, GPU effects (auto-framing, retouch), virtual camera registration, preview — C API in [IPhoneWebcamEngine.h](windows/engine/IPhoneWebcamEngine.h) |
| [windows/source/](windows/source/) | Virtual camera media source DLL, loaded by the Windows Camera Frame Server |
| [windows/common/SharedFrame.h](windows/common/SharedFrame.h) | Shared-memory contract between the engine and the source DLL |
| [windows/cli/](windows/cli/) | `IPhoneWebcamCli.exe`, a console front end for the engine |
| [windows/installer/](windows/installer/) | Inno Setup script and packaging script |
| [windows/tools/](windows/tools/) | `FakePhone` (test stream server) and `CaptureTest` (reads from the virtual camera) |

**Protocol.** The iPhone listens on TCP port 5005 and advertises `_iphonewebcam._tcp` via Bonjour. Each message is `uint32 big-endian length | uint8 type | payload`:
type `1` = H.264 access unit (Annex B; keyframes carry SPS/PPS), type `2` = face detections (`uint8 count`, then `count × (x, y, w, h)` as little-endian float32, normalized to the video frame, origin top-left; sent ~6×/s). Receivers ignore unknown types. The PC can send the byte `K` to request a keyframe. When the network can't keep up, the phone drops frames until the next keyframe.

**Effects pipeline.** Decoded frames stay on the GPU: two pixel-shader passes produce the final 4K NV12 picture — crop/zoom from the face tracker, letterboxing, mirror/rotate, bilateral skin smoothing on luma and redness reduction on chroma, masked by a YCbCr skin-tone classifier and the (motion-extrapolated) face ellipses ([VideoEffects.cpp](windows/engine/VideoEffects.cpp), [FaceTracker.cpp](windows/engine/FaceTracker.cpp)).

**Limitations / ideas.** Wi-Fi only (USB would need usbmuxd-style forwarding); no audio; no zoom/focus/exposure controls; HEVC would halve bandwidth but Windows needs the paid HEVC extension to decode it.

## License

[Apache License 2.0](LICENSE)

The desktop app uses [WPF UI](https://github.com/lepoco/wpfui) (MIT License) for its Windows 11 (Fluent) controls, Mica backdrop and theming.
