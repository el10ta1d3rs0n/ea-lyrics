# 🎤 ea-lyrics

> **Because humming the melody and mumbling fake words only gets you so far.**

**`ea-lyrics`** is a lightweight, zero-bloat Terminal User Interface (TUI) written in pure C that fetches, syncs, and romanizes lyrics in real time.

Built specifically as the **official working lyrics companion for [`ea-music`](https://github.com/el10ta1d3rs0n/ea-music)**, it hooks directly into `mpv`'s IPC socket to turn your terminal into a full-blown, hyper-responsive karaoke machine.

## ✨ Features That Slap

* 🤝 **Seamless `ea-music` Integration:** Automatically hunts down active `ea-music` / `mpv` UNIX sockets (`/tmp/ytdlp_mpv_*.sock` or `$XDG_RUNTIME_DIR/ea-music-mpv.sock`)—no manual wiring required.
* 🗣️ **On-the-Fly Romanization:** Powered by `libicu`, it automatically transliterates non-Latin scripts (Japanese, Korean, Chinese, Cyrillic, etc.) into readable Latin characters so you can actually sing along.
* 🌐 **Multi-Provider Waterfall:** If one lyric database comes up empty, it cascades down your preferred list: **LRCLIB**, **NetEase**, **Musixmatch**, **Genius**, and **Kugou**.
* ⏱️ **Live Sync & Offset Tweaking:** Lyrics out of sync by half a second? Nudge the timing live without missing a beat.
* 🧹 **Smart Title Sanitizer:** Automatically strips annoying YouTube cruft like `(Official Music Video)`, `[4K Remaster]`, and `- Topic` before searching so you actually get hits.
* ⚡ **Fast Local Caching:** Saves fetched lyrics locally so replay sessions load instantaneously.
* 🎛️ **Remote `mpv` Control:** Pause, play, and seek tracks directly from the lyrics window.

## 🛠️ Dependencies & Installation

Since `ea-lyrics` is written in C with zero heavy frameworks, you just need a C compiler, `pkg-config`, and a few standard development libraries:

* `libcurl` (for fetching lyrics)
* `ncurses` / `ncursesw` (for that sweet terminal UI)
* `libicu` (`icu-uc` & `icu-i18n` for transliteration / romanization)
* `pthread` & `libm` (standard POSIX threads & math)

### 1. Install Dependencies

**Arch Linux:**

```bash
sudo pacman -S curl ncurses icu gcc pkgconf
```

**Debian / Ubuntu:**

```bash
sudo apt install libcurl4-openssl-dev libncursesw5-dev libicu-dev build-essential pkg-config
```

### 2. Build & Install (Pick Your Poison)

#### Option A: Arch Linux `PKGBUILD` (Easy Mode)

If you're on Arch (btw), don't even sweat it—just grab the ready-to-go `PKGBUILD` straight from the [**Releases**](https://github.com/el10ta1d3rs0n/ea-lyrics/releases) section of this repo, run `makepkg -si`, and you're golden!

#### Option B: Compile From Source Like a True C Wizard

Clone the repo and slap this bad boy straight into your terminal:

```bash
git clone https://github.com/el10ta1d3rs0n/ea-lyrics.git
cd ea-lyrics

cc -O2 -Wall -Wextra -D_GNU_SOURCE -D_DEFAULT_SOURCE -pthread \
  -o ea-lyrics ea-lyrics.c \
  $(pkg-config --cflags --libs ncursesw libcurl icu-uc icu-i18n) \
  -lm -pthread
```

Then move it somewhere in your `$PATH` (like `/usr/local/bin/` or `~/.local/bin/`) and use it:

```bash
sudo mv ea-lyrics /usr/local/bin/
```

## 🚀 Usage

1. Fire up [**`ea-music`**](https://github.com/el10ta1d3rs0n/ea-music) and start playing a banger.
2. In another terminal pane (or tmux split), run:

   ```bash
   ea-lyrics
   ```

3. Watch the magic happen and sing your lungs out.

### CLI Flags

* `-s /path/to/mpv.sock` — Manually point `ea-lyrics` to a custom `mpv` IPC socket if you're running a standalone `mpv` instance.
* `--gen-config` — Generate a default configuration file at `~/.config/ea-lyrics/ea-lyrics.conf`.

## ⌨️ Default Keybindings

| Key | Action |
| :--- | :--- |
| **`r`** | Toggle romanization on/off |
| **`h`** | Hide/show the secondary (original or romanized) line |
| **`s`** | Manual search (if a weird track title needs human intervention) |
| **`p`** | Change provider priority order on the fly |
| **`]`** / **`[`** | Adjust lyric sync offset (`+0.5s` / `-0.5s`) |
| **`Space`** | Pause / Resume playback in `mpv` |
| **`←`** / **`→`** | Seek backward / forward 5 seconds |
| **`q`** | Quit `ea-lyrics` |

## ⚙️ Configuration

Want romanized lyrics on top and original script dimmed underneath? Want to rebind every key? Generate your config file first:

```bash
ea-lyrics --gen-config
```

Then edit `~/.config/ea-lyrics/ea-lyrics.conf` to your heart's content:

```ini
# ea-lyrics configuration

romanize_default=1
romanized_primary=1
hide_secondary=1
line_spacing=0
primary_bold=1
secondary_bold=0
secondary_dim=1
cache_dir=/tmp/cache/ea-lyrics

# Comma-separated list of lyric providers in order of priority.
# Options: lrclib, netease, musixmatch, genius, kugou
providers=lrclib,netease,musixmatch,genius,kugou

# Keybindings
key_romanize=r
key_search=s
key_quit=q
key_pause=space
key_offset_up=]
key_offset_down=[
key_toggle_hide=h
key_provider_menu=p
```

## 🍴 Fork It! (Yes, You!)

Listen here, you absolute legend: **FORK THIS REPO.**

Don't just sit there staring at the code wishing a color was slightly bluer or wishing it supported some obscure lyric API from 2004—hit that **Fork** button right now! I warmly welcome your weird experiments, chaotic pull requests, and unhinged feature ideas with open arms and a fresh cup of coffee. Break it, fix it, make it yours, and if you make something cool, throw a PR back my way or I'll be mildly grumpy about missing out on your genius! 😤❤️

## 📜 License

This project is open-source and licensed under the **MIT License** (see the [`LICENSE`](LICENSE) file for the boring legal text).

In plain English: do whatever the heck you want with it—copy it, modify it, fork it, ship it, or print it out and frame it on your wall—just keep the original copyright notice intact and don't blame me if your terrible karaoke singing annoys your roommates.

## 🔗 Part of the `ea` Ecosystem

* [**`ea-music`**](https://github.com/el10ta1d3rs0n/ea-music) — The main music player that `ea-lyrics` pairs with.

*Happy singing!* 🎶
