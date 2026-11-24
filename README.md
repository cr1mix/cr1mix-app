<div align="center">

<img src="docs/img/banner.png" alt="cr1mix app" width="100%">

<br>

<a href="../../releases/latest"><img src="docs/img/btn-download.png" height="52" alt="Download"></a>&nbsp;&nbsp;<a href="https://cr1mix.pages.dev"><img src="docs/img/btn-website.png" height="52" alt="Website"></a>&nbsp;&nbsp;<a href="https://discord.com/invite/NjkgT7vXBb"><img src="docs/img/btn-discord.png" height="52" alt="Discord"></a>

<br><br>

<img src="docs/img/shot-home.png" alt="Home" width="100%">

<sub>Live processor, graphics and memory graphs, with restore point, registry backup and Quick setup one click away.</sub>

</div>

<br>

cr1mix app is a free and open source Windows tweaking utility built from scratch in C++20. It ships as a single 1.3 MB executable with no runtime and no background service.

Every tweak is written by hand, says what it does in plain words, and can be switched back. Nothing changes until you choose it.

<br>

## Features

<img src="docs/img/features.png" alt="Features" width="100%">

<br>

## A closer look

### Safe tweaks

<img src="docs/img/shot-safe.png" alt="Safe tweaks" width="100%">

Safe tweaks open as a checklist, grouped by category. Read what each one does, untick what you want to keep, then apply the rest in one go.

<br>

### Quick setup

<img src="docs/img/shot-setup.png" alt="Quick setup" width="100%">

A fresh Windows in minutes. Quick setup asks about Defender, updates, apps, AI, looks, power, services and privacy, then builds the whole plan for you and shows it before anything changes.

<br>

### CPU

<img src="docs/img/shot-cpu.png" alt="CPU" width="100%">

A power plan built from Ultimate Performance, with a laptop profile that keeps battery savings. Timer resolution, foreground priority, core parking and power throttling, each explained on its own card.

<br>

### GPU

<img src="docs/img/shot-gpu2.png" alt="GPU" width="100%">

The NVIDIA 3D profile is set through the driver itself, with no extra tools. Multi-plane overlay, GPU scheduling and interrupts are one tap each, and only the tweaks for your card show up.

<br>

### Services and apps

<img src="docs/img/shot-services.png" alt="Services and apps" width="100%">

Service presets go from Light to Extreme, and you keep any service you still use. Remove the apps you never asked for, turn off Copilot and Recall, and install a browser in one go.

<br>

## Tweaks

307 tweaks across 10 categories, each one explained on its own card.

<img src="docs/img/tweaks.png" alt="Tweak categories" width="100%">

<br>

## Safety

Everything cr1mix app changes can be undone.

<img src="docs/img/safety.png" alt="Safety" width="100%">

<br>

## Getting started

<img src="docs/img/steps.png" alt="Getting started" width="100%">

<br>

<img src="docs/img/requirements.png" alt="Requirements" width="100%">

<br>

<img src="docs/img/note.png" alt="SmartScreen note" width="100%">

<br>

## Building from source

Requires [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) `20261104` (ucrt, x86_64), extracted to `.tools\llvm-mingw-20261104-ucrt-x86_64`.

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

The build writes `build\cr1mix.exe` and `build\cr1mix app setup.exe`.

<img src="docs/img/structure.png" alt="Project structure" width="100%">

<br>

<div align="center">
<sub>Open source under the <a href="LICENSE">GPL-3.0 license</a>. © 2026 cr1mix.</sub>
</div>
