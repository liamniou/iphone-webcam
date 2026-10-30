# iPhone Webcam

[![Build](https://github.com/liamniou/iphone-webcam/actions/workflows/build.yml/badge.svg)](https://github.com/liamniou/iphone-webcam/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

Use your iPhone as a **4K 30 fps webcam** for Windows 11 over Wi-Fi — a small, open-source alternative to Camo Studio.
The iPhone streams H.264 video to a desktop app that feeds a virtual camera called **iPhone Webcam**, which you can pick in Zoom, Teams, Google Meet, OBS, Discord, the Windows Camera app, …

- 3840×2160, 1920×1080 or 1280×720 at 30 fps (the app using the camera picks the size)
- GPU-accelerated decoding, low latency, automatic discovery and reconnection
- Desktop app with live preview, status, mirror/rotate, system tray and *Start with Windows*
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
Choose **one** of these options:

### Option A — from Windows with Sideloadly (no Mac needed)

1. Download `IPhoneWebcam-<version>-unsigned.ipa` from the [latest release](https://github.com/liamniou/iphone-webcam/releases/latest).
2. Install **iTunes** and **iCloud** for Windows — the versions downloaded from apple.com, *not* the Microsoft Store ones (Sideloadly needs their drivers; the Sideloadly website links to the right downloads).
3. Install [Sideloadly](https://sideloadly.io/) and start it.
4. Connect the iPhone with a USB cable, unlock it and tap **Trust** on the phone.
5. In Sideloadly: drag the `.ipa` onto the window, select your iPhone, enter your Apple ID and click **Start**. Enter the verification code if asked.
   - If you get an error that the bundle ID isn't available, open *Advanced options* and enable **Change bundle ID** (use e.g. `com.yourname.iphonewebcam`).
6. On the iPhone:
   - enable **Developer Mode**: *Settings → Privacy & Security → Developer Mode* → on, then restart and confirm;
   - trust your certificate: *Settings → General → VPN & Device Management* → your Apple ID → **Trust**.
7. Open **iPhone Webcam** and allow **Camera** and **Local Network** access.

To renew after 7 days, repeat step 5 (or enable Sideloadly's automatic refresh).

### Option B — AltStore

[AltStore](https://altstore.io/) (with AltServer for Windows or macOS) can install the same `.ipa` and refreshes it automatically every week while your phone and PC are on the same Wi-Fi. Follow AltStore's setup guide, then in AltStore tap **My Apps → +** and pick the `.ipa`.

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
   If the phone isn't found (some routers block discovery), type the IP shown in the iPhone app into *Connect by IP address*.
2. Mount the iPhone in **landscape** (either direction — the image is kept upright). The app must stay open in the foreground; the screen won't auto-lock.
3. In your video app choose the camera **iPhone Webcam**. Apps such as Teams or Zoom usually pick 1080p/720p; in OBS or the Windows Camera app you can select 3840×2160.

Closing the desktop window keeps the camera running in the **system tray** (right-click the tray icon to *Exit*). Options in the app: mirror, rotate 180°, automatic reconnection, GPU decoding and *Start with Windows*. The camera switch button on the iPhone toggles front/back camera.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| The setup says this PC isn't supported | You downloaded the wrong architecture (x64 vs arm64), or the PC runs Windows 10. |
| Red banner "The virtual camera isn't available" | Reinstall with the setup program (it registers the camera). Windows 11 is required. |
| The phone doesn't appear in the list | Same network? App open on the iPhone? *Settings → Privacy & Security → Local Network → iPhone Webcam* enabled? Guest/public Wi-Fi often isolates devices. Use *Connect by IP address*. |
| Video apps show a gray picture | The desktop app isn't receiving video — check its status panel. |
| Choppy video / freezes | 4K needs ~30 Mbit/s: move closer to the router, use 5/6 GHz Wi-Fi, connect the PC by Ethernet. |
| Decoder shows `CPU (software)` and < 30 fps | GPU decoding isn't available: update the graphics driver. |
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
.\build\x64\native\FakePhone.exe --width 3840 --height 2160   # serves a moving 4K test pattern on 127.0.0.1:5005
# In the desktop app, connect to 127.0.0.1 (or: .\build\x64\app\IPhoneWebcamCli.exe --ip 127.0.0.1)
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
| [windows/desktop/](windows/desktop/) | Desktop app (C# WPF, .NET 10): preview, device list, status, tray |
| [windows/engine/](windows/engine/) | `IPhoneWebcamEngine.dll` (C++): discovery, connection, GPU H.264 decoding, virtual camera registration, preview — C API in [IPhoneWebcamEngine.h](windows/engine/IPhoneWebcamEngine.h) |
| [windows/source/](windows/source/) | Virtual camera media source DLL, loaded by the Windows Camera Frame Server |
| [windows/common/SharedFrame.h](windows/common/SharedFrame.h) | Shared-memory contract between the engine and the source DLL |
| [windows/cli/](windows/cli/) | `IPhoneWebcamCli.exe`, a console front end for the engine |
| [windows/installer/](windows/installer/) | Inno Setup script and packaging script |
| [windows/tools/](windows/tools/) | `FakePhone` (test stream server) and `CaptureTest` (reads from the virtual camera) |

**Protocol.** The iPhone listens on TCP port 5005 and advertises `_iphonewebcam._tcp` via Bonjour. Each message is `uint32 big-endian length | uint8 type (1 = H.264 access unit, Annex B) | payload`; keyframes carry SPS/PPS. The PC can send the byte `K` to request a keyframe. When the network can't keep up, the phone drops frames until the next keyframe.

**Limitations / ideas.** Wi-Fi only (USB would need usbmuxd-style forwarding); no audio; no zoom/focus/exposure controls; HEVC would halve bandwidth but Windows needs the paid HEVC extension to decode it.

## License

[Apache License 2.0](LICENSE)
