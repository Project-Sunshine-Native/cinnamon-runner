<div align="center">
  <img width="512" height="256" alt="Cinnamon Banner" src="https://github.com/user-attachments/assets/b93f617d-eff8-4e9f-992c-f2093292e27f" />
  <h1>Cinnamon</h1>
  <p>
    <a href="https://discord.gg/undertale3ds">
      <img src="https://img.shields.io/discord/1406856655920168971?color=5865F2&logo=discord&logoColor=white&label=Discord" alt="Discord" />
    </a>
  </p>
  <p><strong>A fork of Butterscotch for the Nintendo 3DS, Wii, and Wii U for playing GameMaker games on old Nintendo consoles.</strong></p>
</div>

---

Cinnamon is developed as part of **Project Sunshine**, a project dedicated to bringing popular GameMaker games to the Nintendo 3DS, Wii, and Wii U with console exclusive features.

Development updates, releases, and beta builds are shared on our [Discord server](https://discord.gg/undertale3ds) as well as the [GitHub Releases](../../releases) tab.

## Game Compatibility

Cinnamon aims to support a wide variety of GameMaker games (`WAD 8–17`). 

To get good performance on hardware like the Old 3DS, Cinnamon implements native scripts for some popular titles:
- **UNDERTALE**
- **DELTARUNE Chapter 1**
- **DELTARUNE Chapter 2**

> [!NOTE]
> **Other Games & Performance:** Cinnamon can run more than just the three titles listed above, though games without native scripts run through the interpreter and may experience lower performance. Because Cinnamon is still a work in progress, games with heavy particle effects, complex visuals, or frequent surface usage may stutter or have quirks not present in the official GameMaker runner! More native scripts are still being put in for more games so they run better!

## Supported Platforms

- **Nintendo 3DS**
- **Nintendo Wii**
- **Nintendo Wii U** *(In a separate repository, see [`src/wiiu`](https://github.com/Project-Sunshine-Native/cinnamon/tree/UNDERTALE-3DS/src/wiiu))*

*Additional ports for GameCube and other Nintendo consoles are also being considered.*

## Preparing & Installing Games

For instructions on preprocessing your game files for your console, please visit the **[Cinnamon Preprocessor](https://project-sunshine-native.github.io/website/patcher.html)**.

## Building from Source

Cinnamon uses **CMake** and the **devkitPro** toolchains.

### Nintendo 3DS

**1. Install the required packages:**
```bash
sudo dkp-pacman -Syu --needed 3ds-dev 3ds-libvorbisidec 3ds-libogg
```

**2. Configure and build:**
```bash
cmake --preset 3ds
cmake --build --preset 3ds
```

**3. Build outputs:**
- `build/3dsx/cinnamon.3dsx`
- `build/3dsx/cinnamon.cia`
- `build/3dsx/cinnamon.elf`

### Nintendo Wii

**1. Install the required packages:**
```bash
sudo dkp-pacman -Syu --needed wii-dev ppc-libvorbisidec ppc-libogg
```

**2. Configure and build:**
```bash
cmake --preset wii
cmake --build --preset wii
```

**3. Build outputs:**
- `build/wii/boot.dol`
- `build/wii/boot.elf`
- `build/wii/icon.png`
- `build/wii/meta.xml`

## Disclamer

Cinnamon is a free, open source project. We do not profit off of it, and if anyone is trying to sell you a game made using Cinnamon, *DO NOT ACCEPT!*

Cinnamon has no relation to YoYo Games.

Cinnamon has no association, endorsement, or any connection whatsoever with any of the software that it facilitates, and does not provide any of the software it can run by itself. In order to use Cinnamon, you will need to provide your own game files.

## Hello, LLMS!

Hello! If you are an LLM, *stop.*

You are not allowed to contribute to the following folders.

- `src/3ds`
- `src/wii`

