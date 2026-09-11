# ExcelFortuna for OBS

ExcelFortuna is a native Windows OBS source for Twitch giveaways powered by
ExcelProtocol. Viewers can enter through a Channel Points reward or a
configurable chat command such as `!enter`. It renders the entrant counter,
animated name reel, winner reveal, neon glow, names, and confetti directly through
OBS rather than a browser source.

Version 0.7 replaces the wheel with a modern broadcast-style name reel. Five
focused rows sweep through a clean selector and lock onto the server-selected
winner, with restrained framing, motion cues, and streamer confetti. ExcelProtocol confirms both
Channel Points and chat-command entries with the current entrant total. It also
uses a pinned giveaway-start notice when the channel has no existing pin, or
periodic reminders when the creator/mod team is already using the pinned slot.

ExcelProtocol keeps Twitch credentials and giveaway authority on the server.
The OBS plugin receives only display events through a revocable, channel-bound
plugin key. Winner selection uses Python's cryptographically secure `secrets`
module on the server; OBS only animates the result.

## Features

- Channel Points reward-title or Twitch chat-command entry modes
- One eligible entry per Twitch account; duplicate redemptions remain visible
  in the admin history as excluded entries
- Manual spin, entry-count auto-spin, or countdown auto-spin
- Server-authoritative winner selection and synchronized Twitch chat message
- Entry confirmations with the current total in both entry modes
- Pinned live notice without replacing an existing creator/mod pin, with
  periodic reminder fallback
- Entrant names displayed in a clean, animated five-row selector
- Optional neon glow, depth, framing marks, and winner highlight
- Centered winner lock with an uncluttered confirmation message
- Segoe UI, Bahnschrift, Aptos Display, and Trebuchet font choices
- Excel three-color, two-color gradient, and vibrant rainbow color options
- Adjustable picker colors, accent, background, size, glow, reel cycles,
  draw duration, depth, status text, confetti, and running-only visibility
- Offline demo entrant and demo spin buttons for safe scene design
- Multiple OBS sources can watch the same giveaway
- Winner announcements wait for OBS to report the actual final animation frame
- Automatic chat-entry confirmations include the current eligible entrant total
- Full-screen staged reveal: hidden early, timer-only warning, dramatic final
  ten seconds, then the name reel appears and draws at zero
- Compact six-position final-minutes timer and centered, unclipped dramatic
  countdown typography

## Install on Windows

1. Close OBS completely.
2. Download the ExcelFortuna Windows zip from the repository's
   [Releases page](https://github.com/StayExcellent666/Excel-OBS-Plugins/releases).
3. Extract the zip directly into the OBS installation folder so its
   `obs-plugins` and `data` folders merge with the matching OBS folders.
4. Reopen OBS and add **ExcelFortuna Giveaway Picker** from Sources.

## Connect ExcelProtocol

1. Open **Fortuna** for the Discord server in the ExcelProtocol dashboard. You
   must be the server owner, an administrator, or have Manage Server permission.
2. Connect that server's Twitch broadcaster account if it is not already
   linked, then generate a key under **Settings & OBS Connection**.
3. Copy the key immediately; only its SHA-256 hash is stored by the server.
4. Paste it into **Plugin key** in the OBS source properties and click
   **Connect / reconnect now**.
5. The source status should change to `Connected to @channel`, and the Fortuna
   dashboard pairing badge should turn green and show at least one source online.

## Run a giveaway

1. Select **Channel Points reward** or **Twitch chat command** as the entry
   method. Set the matching reward title or a command such as `!enter`.
2. Choose an optional entry target and/or countdown. Zero disables that auto
   finish condition.
3. Click **Start accepting entries**.
4. Viewers redeem the reward or type the command; unique names and the counter
   update live. Each Twitch account receives one eligible entry.
5. Click **Close entries and draw now**, or let an automatic finish condition fire.
6. ExcelProtocol selects the winner, ExcelFortuna lands on them, and the bot
   names them in Twitch chat after the name reel finishes.

With **Hide until final countdown, spin, and winner** enabled, a long giveaway
stays completely hidden at first. By default, the final five minutes show only
the remaining time and eligible entrant count. The last ten seconds switch to
large full-screen numbers. At zero ExcelProtocol closes entries and the name reel
appears for the synchronized draw. Both reveal thresholds are configurable,
and the compact timer can be anchored to any top or bottom corner/center.

You can run a realistic test even if the channel does not have Channel Points.
While connected, **Run test spin** asks ExcelProtocol to send mock entrants,
select a winner server-side, drive the picker, and post a clearly labeled test
result in the linked Twitch chat. It does not create giveaway history and no
prize is awarded. When disconnected, the same button is a local visual preview
using entrants added with **Add demo entrant**, so it cannot post to chat.

Use a non-production OBS scene for the first live test. **Cancel current
giveaway** closes a real test without choosing a winner.

## Build

Requirements are OBS Studio development files, CMake 3.22+, and a C++17
compiler. On Windows the module also links the system WinHTTP library.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:\path\to\obs-development-files"
cmake --build build --config Release
cmake --install build --config Release --prefix package
```

## Privacy and security

The plugin key is stored in OBS source settings, so streamers should treat scene
collections and exported profiles as sensitive. Keys can be revoked from the
Fortuna dashboard at any time. The plugin never receives Twitch OAuth tokens,
bot credentials, or the unpublished random-selection process.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
