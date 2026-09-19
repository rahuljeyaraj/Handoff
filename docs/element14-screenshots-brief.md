# Prompt — app screenshots for the element14 blog, chapters 2–4

*Written 19 Sep 2026 for the next session. Paste everything below the line.*

---

Chapters 2–4 of `docs/element14-blog.md` are the app's user guide, told as a story (Rohit at the expo). They have no pictures. Take screenshots of every stage on the bench phone, put them together into image strips, add them to the post, and cut text the pictures make unnecessary.

**I approve, for this session:** driving the phone with `adb shell input tap/text/keyevent`, and swapping the app's contacts database with the staged story data (backup first, restore at the end). Last session the auto-mode classifier blocked both; if it blocks again, stop and ask me to approve the command. Don't work around it.

## Before you start

- Read memory `android-app-rebuild` (bench walk, tap coordinates, the screencap recipe), `oneplus-ble-filter-slots`, `diagram-style`, `element14-blog`.
- Phone: OnePlus, 1080x2412, density 3 (360 dp wide). `adb exec-out screencap -p > file.png` works from Git Bash. For `run-as ... /data/...` set `MSYS_NO_PATHCONV=1` first. Unlock with `input keyevent 82`. If a picture-in-picture video is on screen, close it first; it covers the bottom of the page.
- App theme is Light. Keep it Light for every shot except the Dark theme one in 4.4, then set it back.
- Bench band **93D1** (COM7). Pairing fails when the phone's scan-filter slots are full (`MEMORY_CAPACITY_EXCEEDED` in logcat, or `locate: not found after 0 results`). Fix: `adb shell am force-stop com.heytap.accessory`, then `adb shell cmd bluetooth_manager disable` / `enable`, then pair within a minute.
- **Back up the database first.** Stop the app, then pull `databases/handoff.db`, `-wal` and `-shm` with `adb exec-out run-as com.handoff.band cat databases/<f>` into `%TEMP%\claude\dbbak-<date>`. Also save `shared_prefs/handoff.prefs.xml`.
- Stage data with `docs/element14-blog/shots/stage-story-db.py <backup> <out> empty|morning|day`. Push the result with the app stopped: `adb exec-in run-as com.handoff.band sh -c "cat > databases/handoff.db"` < file, then remove `handoff.db-wal` and `handoff.db-shm`.

## Rohit's own card (typed into the app's editor)

`Rohit Menon` · `+44 7700 900001` (Mobile) · `rohit.menon@example.com` · `Arclight Embedded` · `Firmware engineer`. In 4.3 he deletes it and sets it up again with the phone label `Custom` → `IRQ`. Ask me before you start if I want different values.

## Shots, stage by stage

Take every stage, one screenshot each. Retake if you catch a ripple, a crossfade or a half-shown snackbar.

**2.1 Pairing with the phone**
1. Setup step 1, the QR viewfinder. **Placeholder:** the band isn't ready, so the real QR scan gets swapped in later. Point the camera at something plain, or cover the lens.
2. The band code dialog with `93D1` typed in. Optional; include it only if the strip needs it.
3. Android's `Allow the app Handoff to access Handoff band 93D1?`
4. Step 2 of 2, `Handoff band 93D1 paired`.

**2.2 Your contact card** (database: `empty`)
1. The empty editor, reached from `Set it up`.
2. The editor filled in with Rohit's card.
3. Home after Save: band box with the green card icon, `No handshakes yet`. Catch the `Saved — writing it to your band` snackbar if you can.

**3.1 First contact** (database: `morning`)
1. Home with `Savithri Raghavan` at the top.
2. Her page: `Met today, 9:14 am`, number and email.

**3.2 Keeping track** (database: `day`)
1. Vikram's page with the renamed name and the note.
2. The edit screen with the name field and `Add a note`.
3. The `Save to phone contacts` button. Stop at Android's contact editor; don't save into the real phone book.
4. Home with `Savithri Raghavan (front desk)` moved up by the second handshake.

**4.1 Home page** (database: `day`)
1. Home, the full list.
2. Search, with something typed that matches a note or an org.
3. Sort switched to `A to Z`.

**4.2 Band info page**
1. The band page: card, battery, Find my band, Vibrate, firmware, Disconnect, Forget.

**4.3 Card page**
1. The card page: `On Handoff band 93D1` tick, pencil and bin.
2. The delete dialog, `Delete your contact card?`
3. Band page with the red, crossed-out card icon.
4. The editor with the phone label menu open (`Mobile`, `Work`, `Home`, `Main`, `Custom`).
5. `Custom` with `IRQ` typed in.
6. The card page again, green.

**4.4 Settings page**
1. Settings.
2. The Theme picker.
3. Home in Dark.

## Put them together

- **One strip per section**: the stages side by side in a row, left to right in order, so a section costs one image's height, not five.
- Crop the status bar and the gesture bar off every shot, so the time and notification icons don't show. Same crop for all of them.
- **Labels inside the picture**: a number and a short caption under each phone ("1 Scan the label", "2 Allow", "3 Paired"). Use arrows between phones where the order matters. Where a tap is what moves you to the next shot, draw a ring round the thing to tap. Only as much text as the picture needs; nothing else inside the image (no title, no footer, no notes).
- Follow `diagram-style`: one uniform font size, solved to fit; colour = whose (Rohit blue, grey for the rest). Ship SVG + PNG. Render the PNG with headless Chrome through `figlib.py` (`CHROME`, `solve`). Keep a generator `gen-NN-<name>.py` beside each figure, with captions in a dict at the top.
- Figure numbers continue after 13 (`13-boards.jpg` is taken): `14-pairing`, `15-your-card`, … in the order drawn. Raw screenshots go in `docs/element14-blog/shots/`.
- The 2.1 strip marks stage 1 as a placeholder in its generator, so the real QR shot is a one-file swap.
- Keep each strip readable at blog width, about 800 px wide on screen. If five phones make the captions too small, split into two rows or drop the optional stage.

## Then the text

- Put each strip under its section heading, or after the story line it shows.
- Cut text that only describes what the picture now shows: 4.1's list of the four buttons, 4.2's row list, 4.3's icon descriptions. Keep the story (people, dialogue, why he does things) and anything the picture can't show (the band's LED and buzz, the no-internet line).
- Show me the cuts as a list before making them. Don't commit until I say so.

## Two places the story and the screens disagree

- The bench band runs off USB, so Battery reads `USB power` where 4.2 says `Full` and 3.1 says three green bars. Ask me: shoot on battery power, or change the line.
- Settings still shows `Language`, which does nothing. The judge review says leave it out of the post, and 4.4 still lists it. Ask me.

## At the end

- Push the database backup back, restore `handoff.prefs.xml` if the theme or card changed, and check the app shows my bench contacts again.
- Leave 93D1 paired and connected.
- Update memory `element14-blog` with the figure numbers.
