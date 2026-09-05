![chatterinoLogo](https://user-images.githubusercontent.com/41973452/272541622-52457e89-5f16-4c83-93e7-91866c25b606.png)

# Chatterino7 with Kick.com Support

[![GitHub Actions Build](https://github.com/sambegui/chatterino7/actions/workflows/build.yml/badge.svg?branch=001-kick-twitch-merge)](https://github.com/sambegui/chatterino7/actions?query=workflow%3ABuild+branch%3A001-kick-twitch-merge) ![Kick.com Support](https://img.shields.io/badge/Kick.com-Supported-00e701) [![Chocolatey Package](https://img.shields.io/chocolatey/v/chatterino7?include_prereleases)](https://chocolatey.org/packages/chatterino7)

**A fork of [Chatterino7](https://github.com/SevenTV/chatterino7) with full Kick.com + Twitch merge support.**

Maintains all original 7TV features while adding comprehensive Kick.com chat integration. This is the go-to chat client for streamers and viewers who use both Twitch and Kick.com.

---

## Features

### Kick.com Integration (NEW)

- **Full Kick.com Chat Support** - Connect to any Kick.com channel with full chat functionality
- **Merged Kick + Twitch Chat Views** - Combine chats from both platforms into a single unified view
- **Platform Indicator Badges** - Visual badges showing which platform each message came from
- **Live Status Indicators** - Red dot and [LIVE] indicator for live Kick streams
- **7TV Emotes for Kick Channels** - Full 7TV emote support including paints and badges
- **Kick OAuth Authentication** - Secure login to your Kick.com account

### Original Chatterino7 Features

- 7TV Name Paints
- 7TV Personal Emotes
- 7TV Animated Profile Avatars
- 4x Images (7TV and FFZ)
- All standard Chatterino 2 features

### Screenshots

![Example of Personal Emotes](https://user-images.githubusercontent.com/27637025/227032811-837c56eb-7724-431b-b00e-b944c9289dff.png)
![Example of Paints](https://user-images.githubusercontent.com/27637025/227034147-cb1fcd76-dbae-4878-9551-96ffa64dd1a9.png)

---

## Downloads

**Stable builds** can be downloaded from the [releases section](https://github.com/sambegui/chatterino7/releases/latest).

To test new features, you can download the **nightly build** [here](https://github.com/sambegui/chatterino7/releases/tag/nightly-build).

Windows users can install Chatterino7 [from Chocolatey](https://chocolatey.org/packages/chatterino7).

### Platform Support

| Platform            | Status    |
| ------------------- | --------- |
| Windows             | Supported |
| macOS (Universal)   | Supported |
| Linux (AppImage)    | Supported |
| Linux (Flatpak)     | Supported |
| Linux (Ubuntu .deb) | Supported |

---

## Issues

- **Kick.com issues**: Report Kick-related bugs or feature requests [in the issue section](https://github.com/sambegui/chatterino7/issues)
- **7TV issues**: Report 7TV-related issues [in the upstream issue section](https://github.com/SevenTV/chatterino7/issues)
- **Core Chatterino issues**: Report core functionality issues [in the Chatterino2 issue section](https://github.com/Chatterino/chatterino2/issues)

---

## Discord

Join the official 7TV Discord for community support: <https://discord.com/invite/7tv>

---

## Building

### AVIF Support

When building Chatterino7, you might not have access to a static build of `libavif`. In that case, you can define `CHATTERINO_NO_AVIF_PLUGIN` in CMake. If you have `qavif.so` from [kimageformats](https://invent.kde.org/frameworks/kimageformats) installed on your system, Chatterino will pick it up and use AVIF images.

### Getting the Source

```shell
git clone --recurse-submodules https://github.com/sambegui/chatterino7.git
cd chatterino7
git checkout 001-kick-twitch-merge
```

or

```shell
git clone https://github.com/sambegui/chatterino7.git
cd chatterino7
git checkout 001-kick-twitch-merge
git submodule update --init --recursive
```

### Build Instructions

- [Building on Windows](./BUILDING_ON_WINDOWS.md)
- [Building on Windows with vcpkg](./BUILDING_ON_WINDOWS_WITH_VCPKG.md)
- [Building on Linux](./BUILDING_ON_LINUX.md)
- [Building on macOS](./BUILDING_ON_MAC.md)
- [Building on FreeBSD](./BUILDING_ON_FREEBSD.md)

---

## Acknowledgments

This project is built on top of:

- [Chatterino7](https://github.com/SevenTV/chatterino7) by SevenTV
- [Chatterino2](https://github.com/Chatterino/chatterino2) by Chatterino Contributors

---

## Code Style

The code is formatted using [clang-format](https://clang.llvm.org/docs/ClangFormat.html). Our configuration is found in the [.clang-format](.clang-format) file in the repository root directory.

For more contribution guidelines, take a look at [the wiki](https://wiki.chatterino.com/Contributing%20for%20Developers/).

## Git Blame

This project has big commits in the history which touch most files while only doing stylistic changes. To improve the output of git-blame, consider setting:

```shell
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

This will ignore all revisions mentioned in the [`.git-blame-ignore-revs` file](./.git-blame-ignore-revs). GitHub does this by default.
