# ExcelFortuna for OBS

ExcelFortuna is a native Windows OBS source for Twitch Channel Points
giveaways powered by ExcelProtocol. It renders the entrant counter, animated
wheel, winner reveal, neon glow, recent names, and confetti directly through
OBS rather than a browser source.

ExcelProtocol keeps Twitch credentials and giveaway authority on the server.
The OBS plugin receives only display events through a revocable, channel-bound
plugin key. Winner selection uses Python's cryptographically secure `secrets`
module on the server; OBS only animates the result.

## Features

- Exact Channel Points reward-title matching
- One eligible entry per Twitch account; duplicate redemptions remain visible
  in the admin history as excluded entries
- Manual spin, entry-count auto-spin, or countdown auto-spin
- Server-authoritative winner selection and synchronized Twitch chat message
- Entrant names drawn directly on seamless, black-outlined wheel slices
- Excel three-color, two-color gradient, and vibrant rainbow palettes
- Adjustable wheel colors, accent, background, size, glow, rotation count,
  spin duration, status text, confetti, and running-only visibility
- Offline demo entrant and demo spin buttons for safe scene design
- Multiple OBS sources can watch the same giveaway

## Install on Windows

1. Close OBS completely.
2. Copy `obs-plugins/64bit/obs-excelfortuna.dll` into the matching folder in
   the OBS installation.
3. Copy `data/obs-plugins/obs-excelfortuna` into the matching OBS data folder.
4. Reopen OBS and add **ExcelFortuna Giveaway Wheel** from Sources.

## Connect ExcelProtocol

1. Open the private **Fortuna** dashboard tab as an ExcelProtocol owner/admin.
2. Under **ExcelFortuna OBS Connection**, enter the Twitch channel login and
   generate a key.
3. Copy the key immediately; only its SHA-256 hash is stored by the server.
4. Paste it into **Plugin key** in the OBS source properties and click
   **Connect / reconnect now**.
5. The source status should change to `Connected to @channel`.

## Run a giveaway

1. Set the exact title of the Twitch Channel Points reward viewers will redeem.
2. Choose an optional entry target and/or countdown. Zero disables that auto
   finish condition.
3. Click **Start accepting entries**.
4. Viewers redeem the reward; unique names and the counter update live.
5. Click **Close entries and spin now**, or let an auto-spin condition fire.
6. ExcelProtocol selects the winner, ExcelFortuna lands on them, and the bot
   names them in Twitch chat after the wheel finishes.

You can run a realistic test even if the channel does not have Channel Points.
While connected, **Run test spin** asks ExcelProtocol to send mock entrants,
select a winner server-side, drive the wheel, and post a clearly labeled test
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
