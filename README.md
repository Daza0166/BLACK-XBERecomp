# Black - XBE Recomp

A static recompilation of the original Xbox version of Black (2006) into a native 64 bit Windows executable. The game's x86 code is translated to C++ ahead of time, the Xbox kernel, Direct3D and DirectSound calls are implemented on Windows, and everything is drawn with Direct3D 12 and played through XAudio2. Made with the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

## Game Setup

- Put the game files in the `assets` folder.
- Run `Black.exe`.
- You can adjust the recomp settings inside the in-game options menu whilst the game is running, or alternatively via the F10 quick menu.



> [!NOTE]
> No game data, XBE bytes or generated code are in this repository. You must own the game.

## Features

### Display
- **Native Renderer** - The game renders through a native Direct3D 12 renderer, no emulation.
- **Display Mode** - Allows you to change between Fullscreen, Borderless and Windowed modes.
- **Resolution** - Lists every mode your monitor reports and defaults to the desktop mode, replacing the 640x480 lock.
- **Render Resolution** - Renders the game at 20% to 200% of the output resolution, for supersampling or downscaling, independent of the window size.
- **Field of View** - Sets the horizontal field of view during gameplay from 45 to 140 degrees, 70 being the game's own.
- **Max Frame Rate** - Caps the frame rate at your monitor's refresh rate, at a value you pick, or removes the cap, replacing the console's 30 FPS limiter.
- **V-Sync** - Enables V-Sync, reducing tearing.
- **FPS Counter** - Shows the frame rate on screen.

### Graphics
- **MSAA** - Smooths geometry edges with 2x, 4x or 8x MSAA.
- **Anisotropic Filtering** - Forces the selected anisotropic filtering level on all textures.

### Mouse and Keyboard
- **Keyboard Support** - Fully supports keyboard and mouse input.
- **Raw Mouse** - Native raw mouse input driven straight into the camera.
- **Mouse Look Sensitivity** - Adjusts mouse look sensitivity.
- **Invert Mouse Look** - Inverts the vertical look axis.
- **Hold To Aim** - Aims down scopes while the button is held instead of toggling with a tap.
- **Rebinding** - Every action can be rebound from the Mouse and Keyboard page.
- **Custom Menu Cursor** - Put a `cursor.png` (or `cursor.dds`) beside the exe to replace the Windows arrow in the game's menus.

### General
- **Skip Intro** - Skips the intro logo's.

### System Requirements

**Minimum**
- **OS** - Windows 10 64 bit.
- **CPU** - 2 cores with SSE4.2 and POPCNT: Intel Core i3/i5/i7 1st generation (Nehalem) or AMD FX (Bulldozer).
- **GPU** - Direct3D 12 with feature level 11_0: NVIDIA GeForce GT 630 (Kepler), AMD Radeon HD 7750 or Intel HD Graphics 510 (Skylake).
- **Memory** - 4 GB.
- **Storage** - 5 GB for the game files, saves and shader cache.

### Build

- Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (build.bat offers to add the last two), plus Git.
- Clone this repository.
- Put the game files in the `assets` folder.
- Run `build.bat`. The first build recompiles the game and takes a while.
- Run `out\build\release\Black.exe`.
