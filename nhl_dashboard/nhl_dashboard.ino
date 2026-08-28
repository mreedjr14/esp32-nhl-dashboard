// NHL dashboard companion for the Pi - subscribes to two retained MQTT
// topics (Flyers division standings + a compact stat card per roster
// player, both published by bots/alerts/pollers/dashboard_publish.py
// every ~20 min) and cycles between/within them entirely on its own
// timer. The ESP32 never talks to NHL's API itself - it only ever shows
// whatever it last received, same "subscribe and render" shape as
// test_message_receiver.ino, just parsing structured JSON instead of a
// raw string and cycling through multiple screens instead of showing one.
//
// Needs one extra library beyond test_message_receiver.ino's set (all via
// Arduino IDE's Library Manager): ArduinoJson (by Benoit Blanchon).
// Written against ArduinoJson v7's JsonDocument API - if the Library
// Manager installs a v6.x release instead, replace every `JsonDocument`
// below with `DynamicJsonDocument standingsDoc(2048)` (and 4096 for the
// roster one) - v6 sizes documents up front instead of growing them
// automatically.
//
// NOT compiled/verified here - no Arduino toolchain available in this
// environment (unlike every Python change this project, which got
// actually run before being called done). Flash this and see what
// breaks; the WiFi/MQTT/TFT setup boilerplate is copied as-is from your
// already-working test_message_receiver.ino, so that part should be
// solid, but the JSON parsing and rendering are new and untested.
//
// Iteration 2 (color fix + shot_range/best_stat fields): confirmed fixed
// live - the color swap theory in FLYERS_ORANGE's comment was correct,
// player name and standings' PHI row both render actual orange now.
//
// Iteration 3 (mini shot heat map): confirmed working live - the
// two-column skater layout (stats left, heat map right) renders
// correctly, screen really was 320x240 with room to spare as the
// User_Setup.h check predicted.
//
// Iteration 4 (full Edge data dump): removed the single "best in the
// league" footer entirely - every Edge metric (renderEdgeList, up to 4
// skaters/5 goalies) is shown now, not just the best one, plus zone time
// by strength and zone starts for skaters (both new, previously 100%
// unused data per stats_api.py). Shot range and the heat map are
// UNCHANGED from iteration 3 per explicit request.
//
// Iteration 5 (white/orange reskin, full-height rink): full visual
// redo of the player card per explicit request. Background is white,
// text black, decorative lines Flyers orange everywhere (not just the
// player name) - fillScreen/setTextColor calls throughout were flipped
// accordingly, including the standings screen and the boot-time
// WiFi-status messages, for a consistent look across every screen this
// thing shows. Player name now explicitly splits "First Last" onto two
// lines instead of relying on println's auto-wrap. The dense
// edge/zone_time/zone_starts/shot_range dump from iteration 4 is gone -
// "I just want to do some stats" below the name now means exactly two
// line-aware header+value pairs (GP/G/A/P/TOI, then #/Pos/Age for
// skaters; Record/GAA/SV%/TOI then #/Pos/Age for goalies) and nothing
// else. renderEdgeList/ordinalSuffix are removed as dead code - the
// payload still carries edge/zone_time/zone_starts/shot_range fields
// (dashboard_publish.py wasn't touched, only what's rendered here), so
// nothing breaks if a future screen wants them back.
//
// The shot map is now the entire right half of the screen (was a
// ~142x114 box in the corner) and draws an actual half-rink schematic -
// rounded boards, goal line, a goal (posts), blue line, and the two
// offensive-zone faceoff circles/dots, all in Flyers orange, using the
// same ZONE_X/ZONE_Y table the heat dots already used so everything
// lines up. Heat dot color is now a real blue->cyan->green->yellow->red
// gradient driven by shot volume (same ratio that already drove dot
// size) via the new heatColor()/rgbColor() helpers, replacing the old
// TFT_RED/TFT_YELLOW/TFT_BLUE percentile coloring - those are plain
// RGB565 literals that never got the R/B-swap compensation FLYERS_ORANGE
// needed (see its comment in setup()), so "red" was almost certainly
// rendering blue-ish on the real panel - exactly the "blue looks like
// the highest value" symptom reported after seeing this live, and the
// reason for this rewrite.
//
// Iteration 6 (bigger/reorganized stats, after seeing iteration 5 live):
// the rink diagram, R/B-swap fix, and reskin all confirmed working
// exactly as intended on the real panel. Only the stat text needed
// another pass - it was still size1 while everything else went bigger,
// and read small/cramped. Now: name is black (not orange - orange is
// reserved for the rink and the two new divider lines between
// name/stats and stats/bio), and the header+value row pairs from
// iteration 5 are gone in favor of one size2 line per stat group with
// inline labels (G18 A37 P55, then GP90 <TOI> on its own line per
// explicit request) - a separate header row would've cost as much
// vertical space as a third stat line at this size. Skaters keep TOI
// unlabeled (squeezed against the rink, ~150px before it starts);
// goalies get the full "TOI" label since they have the whole width free.
//
// Iteration 6 confirmed working live too - reskin, sizing, and dividers
// all landed as intended.
//
// Iteration 7 (goalie table, corner clock, standings relayout): three
// more explicit requests. Goalies no longer get individual cards in the
// SCREEN_ROSTER cycle - skaterCount() relies on dashboard_publish.py
// always appending goalies after forwards/defensemen (never interleaved)
// to find where skaters end, and SCREEN_GOALIES (new enum value) shows
// all of them at once in a table (name/#/age/gp/sv%/gaa) after the last
// skater card, before looping back to standings. A small clock box
// (black text, Flyers-orange border) now sits bottom-left on every
// screen - configTzTime() with a US-Eastern POSIX TZ string
// ("EST5EDT,M3.2.0,M11.1.0") handles EST/EDT DST switching automatically
// via the standard C time API (time()/localtime_r()), no manual offset math;
// it needs its own NTP reachability (independent of the Pi/MQTT side)
// and reads as the 1970 epoch until that first sync completes, so
// renderClock() no-ops until time(nullptr) looks like a real timestamp.
// It repaints on every full screen render plus its own ~1s tick in
// loop() so it doesn't go stale for an entire 15s slide. Standings is
// now a left/right split - a big "NHL / Standings" title occupies the
// left half (freed up by dropping the table to team/gp/pts only, per
// explicit request to make room), the table itself moved to the right
// half, with a new vertical orange divider between them; gp is derived
// client-side (wins+losses+ot_losses) since the payload never carried
// it as its own field.
//
// Iteration 8 (self-serve WiFi + team setup, moved to its own public repo):
// this sketch used to hardcode both the WiFi credentials and the team
// (Flyers-only, via dashboard_publish.py's TEAM constant) - fine for one
// device on one home network, not fine for handing boards to friends on
// their own networks who want their own team. WiFi.begin() with a
// hardcoded ssid/password is gone in favor of WiFiManager: on first boot
// (or whenever a saved network can't be reached), the device puts up its
// own "ESP32-Dashboard-Setup" AP with a captive portal; alongside the
// normal SSID/password fields there's now a custom "Team Abbreviation"
// field (WiFiManagerParameter), saved to flash (Preferences/NVS, not
// WiFiManager's own storage - custom params aren't persisted
// automatically the way SSID/password are) so it survives reboots. MQTT
// topics are no longer the fixed "dashboard/standings"/"dashboard/roster"
// - they're built at runtime from the saved team code
// ("dashboard/PHI/standings" etc.) so multiple teams' data can share one
// broker without devices seeing each other's team.
//
// Known limitation, not solved here: once WiFi + team are saved, the
// portal only reopens if the saved WiFi fails to connect - there's no way
// to change just the team later short of forcing a reconnect failure
// (wrong password temporarily, router off, etc.) or re-flashing. Fine for
// a first pass; a physical "hold button at boot to reconfigure" trigger
// would be the natural follow-up if that turns out to matter.
//
// New library beyond what test_message_receiver.ino needed: WiFiManager
// (by tzapu, via Library Manager). Preferences.h ships with the ESP32
// core, no install needed.
//
// Iteration 9 (portal-configurable time zone + 12h/24h format): the
// clock box was hardcoded to Eastern/12h-with-AM/PM, fine when this was
// one device on one desk, not fine once friends in other zones (one
// explicitly wants 24h/military time) are involved. Two more
// WiFiManagerParameter fields join the team one from iteration 8 - a
// time zone abbreviation (ET/CT/MT/PT/AT, looked up against POSIX TZ
// strings in TZ_TABLE rather than asking anyone to type
// "EST5EDT,M3.2.0,M11.1.0" themselves) and a 12/24 format flag - both
// saved to the same Preferences namespace as team, both defaulting to
// "ET"/"12" (this sketch's original hardcoded behavior) if unset or
// invalid. All three fields now save from one saveConfigParams()
// callback - WiFiManager only supports registering a single
// setSaveParamsCallback.
//
// Still unverified like every .ino change here - flash and check: the
// goalie table doesn't run past the clock box if the roster has more
// goalies than expected, the clock actually shows correct time for
// whatever zone was entered (and flips correctly across a DST boundary,
// not verifiable until one happens), the new standings split doesn't
// clip the divider or the title against the table, that the captive
// portal's team field actually saves and survives a reboot, that a
// lowercase or blank entry doesn't wedge the topic names, and (new in
// iteration 9) that an invalid time zone entry falls back to ET instead
// of silently breaking configTzTime(), and that 24h format actually
// renders as "HH:MM" rather than still trimming a leading zero meant
// only for 12h hours.
//
// Iteration 10 (OTA updates via GitHub Releases): ArduinoOTA (the usual
// push-from-your-laptop approach) only works when you're on the same LAN
// as the device, which friends' devices never are. checkForOTAUpdate()
// instead pulls: it asks GitHub's "latest release" API for this repo's
// newest tag, and if it doesn't match FIRMWARE_VERSION, downloads and
// flashes whatever .bin is attached to that release via HTTPUpdate, then
// reboots itself. Runs once at boot and once a day after that (see
// OTA_CHECK_INTERVAL_MS) - no user action needed on the device end.
//
// This only checks/downloads - it doesn't build or publish anything.
// Cutting a new release is still a manual step: bump FIRMWARE_VERSION
// above, compile via Arduino IDE's Sketch -> Export Compiled Binary
// (produces nhl_dashboard.ino.bin in the sketch folder), commit/push the
// source change, then create a GitHub Release tagged with the *exact*
// same string as FIRMWARE_VERSION (including the "v") and upload that
// .bin as its asset. Forgetting to bump the version, or tagging it
// differently than FIRMWARE_VERSION, means devices already on that
// version never notice the release exists.
//
// New libraries beyond what iteration 8 needed: none - HTTPClient and
// HTTPUpdate both ship with the ESP32 board package already.
//
// Still unverified here too: that GitHub's API response actually parses
// (its JSON is much larger than this sketch's MQTT payloads - deploy and
// watch Serial output on first boot), that a real update completes and
// reboots cleanly rather than bricking mid-flash, and that a device
// already on the latest tag correctly does nothing instead of looping.
//
// Iteration 12 (division name on the standings screen): the title was a
// static "NHL"/"Standings" - fine when every device only ever showed the
// Metropolitan (Flyers') standings, not fine now that each device shows
// whatever division its own team belongs to. dashboard_publish.py's
// standings payload carries a new "division" field (e.g. "Central",
// "Pacific") which replaces the static "NHL" headline; capped at size2
// (was size3) since "Metropolitan," the longest of the four division
// names, only fits this side of the screen at that size. Unverified:
// whether "Metropolitan" at size2 actually clears the vertical divider
// at x=160 rather than running into it - it's a tight fit on paper
// (~144px of a ~152px-wide space).

// Must be the very first include. WiFiManager -> WebServer.h expects the
// bare name "FS" to already mean fs::FS by the time it's compiled, but
// TFT_eSPI.h (below) pulls in FS.h itself first in a way that doesn't
// leave that alias active - the result is a "'FS' was not declared in
// this scope; did you mean 'fs::FS'" build error out of WebServer.h.
// Including it here first locks in FS.h's own `using fs::FS;` before
// anything else touches it (a known WiFiManager/ESP32-core-3.x
// interaction, not specific to this sketch).
//
// Iteration 13 (bigger standings table, seen live on real hardware):
// confirmed live that the division title/clock box changes render as
// intended, but the Team/GP/Pts table itself was still size1 and read
// noticeably smaller than everything else on the screen - renderStandings()
// bumps it to size2, tightening the column format from 6/5/5 to 4/4/4
// characters so the wider glyphs still fit this ~152px-wide half of the
// screen (see the comment at its call site). "Waiting for data..."
// deliberately stays size1 - it's 19 characters, which would run past
// the screen edge at size2.
//
// Iteration 14 (first real OTA test, and the fix it turned up): the
// v1.1.0 release (iteration 13's change) was the first OTA update
// actually attempted against real hardware - it failed with
// HTTP_UE_SERVER_WRONG_HTTP_CODE (-104), root-caused via the new Serial
// logging in checkForOTAUpdate() to GitHub's browser_download_url being
// a redirect (302) to a different host, which HTTPUpdate doesn't follow
// unless told to. httpUpdate.setFollowRedirects() fixes it - see its
// call site. This fix has to reach a device via USB once (a device
// can't OTA its way out of not being able to follow the OTA download's
// own redirect), but every device after that, and every future release,
// should OTA normally from here on.
//
// Iteration 15 (fix a downgrade bug found testing iteration 14's fix):
// after the redirect fix, the very next boot logged "Latest release tag:
// v1.1.0" while running v1.1.1 and tried to update anyway - the version
// check was a plain "does the tag differ" comparison, which treats an
// OLDER published release as reason to update just as readily as a
// newer one. isNewerVersion() replaces that with an actual
// major.minor.patch comparison. Another mandatory USB bootstrap, same
// reasoning as iteration 14 - a device with the old backwards check
// can't be trusted to correctly evaluate whether the fixed version is
// itself "newer".
//
// Iteration 16 (boot-status screen, and the first real no-USB OTA test):
// per explicit request, setup() now shows its progress on the TFT
// instead of just "Connecting WiFi..." the whole time - a
// "Connecting to github..." line while checkForOTAUpdate() runs, then
// "Release vX.Y.Z" (whatever's actually left running after that call
// returns - if an update was applied, the device already rebooted, so
// this always reflects the version that's really live) and a fixed
// "CROSBY SUCKS!!!" line, held for 2 seconds before the slideshow
// starts. This version bump (v1.1.2 -> v1.2.0) is deliberately NOT being
// flashed via USB - both iteration 14 and 15's fixes are already on the
// device, so this is the first release meant to prove the whole OTA path
// actually works unattended, end to end.
//
// Iteration 17 (the v1.2.0 test failed too - a third OTA bug): same
// symptom as iteration 14 (getLastError()==0, empty string, fails in
// ~2s) even with the -104 fix from iteration 14 already in place.
// Confirmed via `curl` from a laptop that the v1.2.0 asset downloads
// completely fine through GitHub's redirect - so the asset itself was
// never the problem. The redirect target is a ~900+ character,
// percent-encoded Azure SAS-token URL; checkForOTAUpdate() no longer
// trusts HTTPUpdate's own redirect-following for the real download at
// all - it resolves the Location header itself first (see the comment
// at that call site) and hands HTTPUpdate an already-final URL. Version
// deliberately reverted to v1.1.2 here (matching iteration 14/15's
// pattern) so this fix can bootstrap via USB and then be tested against
// the *existing* v1.2.0 release live, rather than needing yet another
// release cut.
//
// Iteration 18 (iteration 17's fix had its own bug): manual redirect
// resolution ran but immediately hit "no Location header" even though
// the server does send one - HTTPClient::header() only ever returns
// values for headers pre-registered via collectHeaders(), which iteration
// 17 never called. Every response header was being silently discarded
// regardless of what the server sent. Fixed by registering "Location"
// before the request. Same USB-bootstrap-then-retest-against-v1.2.0
// pattern as iteration 17 - version stays at v1.1.2 here too.
//
// Iteration 19 (redirect resolution finally worked - new failure past
// it): confirmed live - "Redirect request returned HTTP 302" and a
// correctly-resolved 921-char URL, matching what curl found independently.
// The follow-up download connection then failed with
// "HTTP error: connection refused" (error -1) - two WiFiClientSecure/
// HTTPClient TLS sessions open at once (one for redirect resolution, one
// for the real download) was one too many for the ESP32's available TLS
// session memory. Fixed by scoping redirectClient/redirectHttp inside a
// block so they're fully destructed - freeing that memory - before
// updateClient opens the second connection. Still v1.1.2/USB-bootstrap
// pattern.
//
// Iteration 20 (give up on HTTPUpdate's black box, do the download
// manually): confirmed live - redirect resolution now works cleanly
// (HTTP 302, correctly resolved, free heap a healthy 93368 bytes ruling
// out iteration 19's memory theory as the cause of THIS failure) but
// httpUpdate.update() on the resolved URL still failed with the exact
// same uninformative "error 0, empty string" as iterations 14 and 17 -
// three different real bugs in a row hiding behind the identical
// unhelpful symptom. Replaced entirely with a manual HTTPClient GET +
// Update.begin()/writeStream()/end() sequence, each step logging its own
// real HTTP code / byte count / Update.errorString() - no more guessing
// what "error 0" means. httpUpdate/HTTPUpdate.h are gone; Update.h
// (ships with the ESP32 core, no install needed) replaces it. Still
// v1.1.2/USB-bootstrap pattern, still targeting the existing v1.2.0
// release.
//
// Iteration 21 (manual flow finally gave a real answer... partially):
// confirmed live - the download itself is completely healthy (HTTP 200,
// Content-Length 1239488 matching the actual asset size exactly) but
// Update.begin() returned false while errorString() said "No Error".
// Suspected an ESP32-core quirk where the "image bigger than the OTA
// partition slot" check inside begin() logs at a debug verbosity most
// sketches never enable and never sets an error code, making a too-big
// binary and a genuinely-fine begin() look identical. Recommended
// switching to a Partition Scheme with a bigger OTA app slot.
//
// Iteration 22 (iteration 21's theory didn't survive contact with
// hardware): confirmed live - switched to "Minimal SPIFFS (1.9MB APP
// with OTA/128KB SPIFFS)" (1.9MB per OTA slot, comfortably more than the
// 1.24MB image) AND did a full "Erase All Flash Before Sketch Upload" to
// rule out stale OTA-bookkeeping data left over from the old partition
// layout (a real, separate risk when changing partition schemes without
// erasing). Update.begin() failed identically anyway. Rather than guess
// a fourth time, checkForOTAUpdate() now prints ESP.getFreeSketchSpace()
// and the actual running/next-update esp_partition_t details (label,
// address, size) straight from esp_ota_ops.h immediately before
// begin() - ground truth instead of inference, since two theories in a
// row that looked solid on paper turned out wrong.
//
// Iteration 23 (ground truth confirmed size was never the issue):
// confirmed live - Free sketch space 1966080, running partition app0 @
// 0x10000 size 1966080, next update partition app1 @ 0x1f0000 size
// 1966080, all comfortably larger than the 1239488-byte image. Yet
// Update.begin(contentLength) still failed with errorString() "No
// Error." Since the partition itself is provably fine, testing whether
// something in begin()'s exact-size comparison/handling is itself the
// problem by passing UPDATE_SIZE_UNKNOWN instead - the writeStream()
// byte-count check further down already independently confirms the full
// image lands, so begin() doesn't strictly need the exact size upfront.
//
// Iteration 24 (the real answer, from Core Debug Level Verbose): both
// UPDATE_SIZE_UNKNOWN and the exact size failed identically, so raised
// Tools > Core Debug Level from None to Verbose (no code change) to let
// the ESP32 core's own internal log_e()/log_w() calls - which had been
// silenced this whole time - actually reach Serial. Immediately found
// it: "[E][Updater.cpp:293] begin(): _buffer allocation failed". Not a
// partition problem at all (already proven fine) - heap fragmentation.
// This board has no PSRAM, and everything shares one small internal SRAM
// pool; the post-setup memory dump (also a Verbose-level feature) showed
// 145KB technically free but only ~40KB in the single largest
// contiguous block, with the download's own TLS session still holding
// its buffers open at the exact moment Update.begin() needs its own
// chunk (can't close that connection first - writeStream() still needs
// to read from it). Attempted fix: shrinking updateClient's mbedTLS
// buffers via setBufferSizes() - which doesn't compile, see iteration
// 25. Four wrong-or-incomplete theories (iterations 14/17/19 partially,
// 21, 23) before Verbose logging just showed the real error directly -
// worth remembering next time an ESP32 failure looks inexplicable: turn
// that on early, not late.
//
// Iteration 25 (setBufferSizes() doesn't exist in this core version -
// structural fix instead): confirmed via the actual NetworkClientSecure.h
// header for this exact core release (3.3.11) that no such method is
// exposed at all - WiFiClientSecure's mbedTLS buffer sizes aren't
// application-configurable here. Reordered instead: Update.begin() now
// runs BEFORE updateClient/the download's TLS connection are even
// created, so its buffer allocation happens while heap is at its least
// fragmented, rather than competing with an already-open TLS session for
// the same small pool. Content-Length is no longer known until after
// begin() succeeds, so begin() uses UPDATE_SIZE_UNKNOWN (already known
// safe from iteration 23) rather than an exact size.
//
// Iteration 26 (moved the whole OTA check earlier in setup(), before
// MQTT): confirmed live - iteration 25's reordering got Update.begin()
// past its allocation failure, but shifted the same fragmentation
// problem one step later: the download's own TLS connection then failed
// ("HTTP -1: HTTP error: connection refused"). This board just barely
// has room for one big allocation at a time, not both a live TLS session
// and Update's buffer together. client.setBufferSize(16384) (a
// permanent 16KB allocation, held for the sketch's entire life) was
// already carved out before checkForOTAUpdate() ran, for a feature
// (MQTT) not even needed during the update attempt itself. Moved the
// entire OTA check - and its boot-status screen lines, since neither
// depends on MQTT or the team-based topic strings - to run immediately
// after WiFi connects, before any MQTT setup at all. Topic-building,
// espClient/client setup, configTzTime, and reconnectMQTT() now all run
// after the OTA check instead of before it.
//
// Iteration 27 (so close - the full image made it, end() still balked):
// confirmed live - iteration 26's reordering worked as intended (free
// heap 163020 at the start of the check, vs ~93000 before), Update.begin()
// succeeded, and writeStream() delivered the complete image (1239488 of
// 1239488 bytes, exact match) over a ~48s transfer. Update.end() still
// refused, with a real, specific error this time: "Stream Read Timeout".
// Given every byte demonstrably arrived, this reads as a transient stall
// during the transfer (each writeStream() chunk pauses to erase/write
// flash before its next socket read, which can make that next read look
// "slow" against a tight default timeout) rather than an actual dropped
// connection - extended both updateClient's and updateHttp's read
// timeouts to 15s to stop that stall from being flagged as a timeout at
// all.
//
// Iteration 28 (iteration 27's theory was wrong - the real mechanism,
// from reading the actual Updater.cpp source for this exact core
// version): the 15s timeout bump had no effect - identical failure,
// identical exact byte count, identical instant end()-after-write
// timing. Fetched espressif/arduino-esp32's 3.3.11 Updater.cpp directly
// and traced it: UPDATE_SIZE_UNKNOWN (used at begin() so its allocation
// could run before the download connection opens - iteration 25) makes
// Update expect the FULL PARTITION size (1966080 bytes) as the target,
// not this image's real 1239488. writeStream() has no other way to know
// when to stop - after writing all the real data it keeps trying to
// read ~726KB more that will never come, hits 300 failed reads at 100ms
// apart (30s - which is where the extra time was actually going, not a
// transient stall), and marks UPDATE_ERROR_STREAM right as it gives up.
// The written-byte count it returns at that point is still exactly
// correct, since the abort happens on a read that contributed nothing
// new. Fix: since our own written == contentLength check already proves
// the real data is complete, explicitly Update.clearError() when the
// error is specifically UPDATE_ERROR_STREAM, then call Update.end(true)
// (evenIfRemaining) instead of end() - which also corrects _size to
// match actual progress internally, avoiding a second "premature end"
// rejection from the size mismatch that caused this in the first place.
//
// Iteration 29 (iteration 28's clearError() workaround wasn't enough -
// the real fix): confirmed live - end(true) still failed, now reporting
// "No Error" specifically (not "Stream Read Timeout" - so clearError()
// did work), yet still returned false. Read _abort()'s actual source:
// it calls _reset() BEFORE setting the error code, wiping the entire
// update session (partition handle, size, progress, buffer) - not just
// flagging an error. So by the time writeStream()'s 30s timeout fires
// and returns, there's no session left to finish; clearing the error
// code afterward can't undo that reset. The only real fix is preventing
// that timeout from ever firing: added a short-lived, scoped "size
// check" connection (same closed-before-moving-on pattern as the
// redirect resolution above) that learns the exact Content-Length
// before Update.begin() runs, replacing UPDATE_SIZE_UNKNOWN entirely.
// writeStream() now has the real target size from the start, so
// remaining() correctly reaches 0 exactly when the real data ends,
// never entering the timeout path in the first place. The
// clearError()/end(true) workaround from iteration 28 is removed - no
// longer needed once the root cause producing that error is gone, and
// keeping it would have silently masked a genuine future stream failure
// instead of surfacing it.
//
// Confirmed live: this device rebooted itself from v1.1.2 straight to
// v1.2.0 with zero USB involvement, then correctly reported "Already up
// to date" on the next boot - the full OTA path (redirect resolution,
// exact-size lookup, Update.begin/writeStream/end, self-reboot, version
// comparison) working unattended, for the first time, end to end.
//
// v1.3.0 (this bump): the published v1.2.0 release predates every OTA
// fix above (iterations 17-29) - it was built right after iteration 16,
// before the redirect bug was even found. A device that OTAs to v1.2.0
// inherits that old, broken OTA code, right back where this device
// started. This version exists so a fresh gold-master release has every
// fix already in it - both for boards getting flashed straight from this
// source, and so any *future* release these boards OTA to isn't handed
// to code that can't reliably apply it.
//
// Iteration 30 (next-5-games schedule slide, per explicit request): new
// SCREEN_SCHEDULE between standings and the roster cards - one dense
// screen (not cycled one game at a time, per explicit request) showing
// all 5 upcoming games at once: team/home-away/date/time per game (size2,
// primary info) plus venue and US TV networks (size1, secondary) on a
// second line each. dashboard_publish.py's new dashboard/{team}/schedule
// topic carries raw UTC start times rather than pre-formatted ones -
// formatGameDateTime() converts to local time itself (same TZ this
// sketch already configured for the clock via configTzTime()), so the
// same payload renders correctly regardless of which zone a given friend
// picked in the setup portal. callback()/reconnectMQTT() generalized
// from a two-way standings-or-roster split to an explicit per-topic
// dispatch now that there are three topics, not two.
//
// Still unverified like every visual change here: the two-line-per-game
// layout actually stays clear of the clock box at the bottom-left (sized
// deliberately to clear it on paper, see renderSchedule()'s own
// comment), long venue names + multiple TV networks don't run off the
// right edge of the screen, and timegm() is available in this exact
// ESP32 core's newlib (a standard POSIX extension, expected to work, but
// not something used elsewhere in this file before now).

#include <FS.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <string.h>
#include <time.h>

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

// "PHI" is just the fallback if a device somehow ends up with no saved
// team (shouldn't happen post-setup - saveConfigParams() below always
// writes one) - not a "Flyers by default" statement beyond that.
char teamCode[4] = "PHI";
char standingsTopic[24];
char rosterTopic[24];
char scheduleTopic[24];

// Time zone abbreviation + display format, both portal-configurable
// (iteration 9) so a friend in a different zone, or one who wants 24h
// instead of 12h AM/PM, doesn't need a firmware change - see
// renderClock() and setup()'s configTzTime() call. "ET"/"12" match the
// original hardcoded Eastern/12h behavior, so an unrecognized or blank
// entry falls back to exactly what this sketch always did before.
char tzAbbrev[4] = "ET";
char timeFormat[4] = "12";

struct TzEntry { const char* abbrev; const char* posix; };

// POSIX TZ strings (see setup()'s original comment on why - auto DST via
// the standard C time API, no manual offset math). Covers the mainland
// US/Canada zones an NHL-fan friend group is likely spread across; add
// entries here (and to the portal's field hint below) if that's ever not
// enough.
const TzEntry TZ_TABLE[] = {
  {"ET", "EST5EDT,M3.2.0,M11.1.0"},
  {"CT", "CST6CDT,M3.2.0,M11.1.0"},
  {"MT", "MST7MDT,M3.2.0,M11.1.0"},
  {"PT", "PST8PDT,M3.2.0,M11.1.0"},
  {"AT", "AST4ADT,M3.2.0,M11.1.0"},
};
const int TZ_TABLE_LEN = sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]);

const char* posixTzFor(const char* abbrev) {
  for (int i = 0; i < TZ_TABLE_LEN; i++) {
    if (strcmp(TZ_TABLE[i].abbrev, abbrev) == 0) return TZ_TABLE[i].posix;
  }
  return nullptr;
}

// HiveMQ Cloud cluster (replaces the old local-LAN broker at
// 192.168.1.78, which friends' devices on their own networks can't
// reach). mqtt_username/mqtt_password are the RESTRICTED, subscribe-only
// credential set (ACL: subscribe on "dashboard/#" only, no publish) - not
// dashboard_publish.py's publisher credentials. Safe to commit here even
// though this repo is public: worst case if these leak is someone else
// also subscribes to the same public standings/roster data, they can't
// publish or read anything else.
const char* mqtt_server = "52d35eeb56c24538a0883b18917b1ee8.s1.eu.hivemq.cloud";
const uint16_t mqtt_port = 8883;
const char* mqtt_username = "mreedjr14_sub";
const char* mqtt_password = "2!ZT^QMd*5$gHRxN59%U";

// OTA (see checkForOTAUpdate() and this file's Iteration 10 note up top).
// FIRMWARE_VERSION must exactly match a GitHub release's tag name for
// this device to recognize it's already current - bump this and tag the
// release identically when cutting a new version, or every device will
// think that release is newer forever (or, if left the same as an
// already-installed version, never notice it at all).
#define FIRMWARE_VERSION "v1.3.0"
const char* OTA_REPO = "mreedjr14/esp32-nhl-dashboard";
// Once a day - GitHub's unauthenticated API rate limit (60/hr) is no
// concern at that cadence, and firmware doesn't change often enough to
// need faster than daily.
const unsigned long OTA_CHECK_INTERVAL_MS = 24UL * 60 * 60 * 1000;
unsigned long lastOtaCheck = 0;

WiFiManagerParameter* teamParam;
WiFiManagerParameter* tzParam;
WiFiManagerParameter* timeFormatParam;

WiFiClientSecure espClient;
PubSubClient client(espClient);

// Flyers orange (Pantone 172C, f74902) - same color used for the "Flyers"
// preset elsewhere in this project (mikes_automations' /lights command).
uint16_t FLYERS_ORANGE;

// --------------------------------------------------------------------
// Cached data - updated whenever a retained (or new) message arrives on
// either topic. Screen cycling below always renders from these, never
// re-parses anything mid-render, so a message arriving mid-draw can't
// tear a screen in half - it just takes effect next time that screen
// comes back around.
// --------------------------------------------------------------------
JsonDocument standingsDoc;
JsonDocument rosterDoc;
JsonDocument scheduleDoc;
bool haveStandings = false;
bool haveRoster = false;
bool haveSchedule = false;

enum Screen { SCREEN_STANDINGS, SCREEN_SCHEDULE, SCREEN_ROSTER, SCREEN_GOALIES };
Screen currentScreen = SCREEN_STANDINGS;
unsigned long screenChangedAt = 0;
int rosterIndex = 0;

const unsigned long STANDINGS_DURATION_MS   = 15000;
const unsigned long SCHEDULE_DURATION_MS    = 15000;
const unsigned long PLAYER_CARD_DURATION_MS = 15000;

// --------------------------------------------------------------------
// Rendering
// --------------------------------------------------------------------

void renderStandings() {
  tft.fillScreen(TFT_WHITE);

  // Left half: a big two-line title filling the space freed up by
  // trimming the table on the right to 3 columns (per explicit request -
  // this side-by-side layout only fits with the row down to team/gp/pts).
  // Division name (not a static "NHL") is the headline now that this
  // isn't Flyers/Metropolitan-only - every team's own division, from
  // dashboard_publish.py's payload. Capped at size2 rather than the
  // original size3: "Metropolitan" (the longest of the four division
  // names) doesn't fit this ~150px-wide half of the screen at size3,
  // and size2 comfortably fits all four. Falls back to "NHL" if the
  // field's missing (no data received yet, or an older cached payload
  // from before this field existed).
  const char* division = standingsDoc["division"] | "NHL";
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setCursor(8, 76);
  tft.println(division);
  tft.setTextSize(1);
  tft.setCursor(8, 104);
  tft.println("Standings");

  tft.drawFastVLine(160, 0, 240, FLYERS_ORANGE);
  tft.drawFastVLine(161, 0, 240, FLYERS_ORANGE);

  tft.setTextColor(TFT_BLACK, TFT_WHITE);

  if (!haveStandings) {
    // Stays size1 - "Waiting for data..." is 19 characters, which would
    // run past the screen's right edge at size2 (this half of the
    // screen is only ~152px wide). Not worth wrapping logic for a
    // transient loading message.
    tft.setTextSize(1);
    tft.setCursor(168, 8);
    tft.println("Waiting for data...");
    return;
  }

  // Table bumped from size1 to size2 per explicit request (it read too
  // small next to everything else on this screen) - column widths
  // tightened from 6/5/5 to 4/4/4 chars so the row still fits this
  // ~152px-wide half at the bigger font (16 chars * ~6px/char *2 would've
  // run off the screen edge; 12 chars * ~6px/char *2 = ~144px clears it).
  // Still left-justified with the same %-Ns pattern, so "CAR"/"113" etc.
  // never get truncated by the width - it's a minimum, not a cap.
  tft.setTextSize(2);
  char hdr[20];
  snprintf(hdr, sizeof(hdr), "%-4s%-4s%-4s", "Team", "GP", "Pts");
  tft.setCursor(168, 8);
  tft.println(hdr);

  JsonArray rows = standingsDoc["rows"].as<JsonArray>();
  int y = 30;
  for (JsonObject row : rows) {
    bool isTeam = row["is_team"];
    tft.setTextColor(isTeam ? FLYERS_ORANGE : TFT_BLACK, TFT_WHITE);
    tft.setCursor(168, y);

    const char* abbrev = row["abbrev"];
    int wins = row["wins"];
    int losses = row["losses"];
    int otLosses = row["ot_losses"];
    int points = row["points"];
    int gp = wins + losses + otLosses;  // not a payload field - cheap enough to derive here

    char line[20];
    snprintf(line, sizeof(line), "%-4s%-4d%-4d", abbrev, gp, points);
    tft.println(line);
    y += 24;  // was 20 - a bit more room for the taller size2 glyphs
  }
}

// Parses "YYYY-MM-DDTHH:MM:SSZ" (ISO8601 UTC - exactly what NHL's API and
// dashboard_publish.py's schedule payload both use) into local date/time
// strings, honoring the same 12h/24h preference as renderClock(). Doing
// the UTC->local conversion here rather than server-side means one
// payload works correctly for every friend's device regardless of which
// time zone they picked in the setup portal - the device already has
// its own TZ configured via configTzTime() in setup(). timegm() is a
// standard newlib/POSIX extension, expected to be available on ESP32,
// but unverified here like everything else in this file - flash and
// check the actual game times land in the right zone.
void formatGameDateTime(const char* startUtc, char* dateBuf, size_t dateBufSize, char* timeBuf, size_t timeBufSize) {
  struct tm tmUtc = {};
  int year, month, day, hour, minute, second;
  sscanf(startUtc, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
  tmUtc.tm_year = year - 1900;
  tmUtc.tm_mon = month - 1;
  tmUtc.tm_mday = day;
  tmUtc.tm_hour = hour;
  tmUtc.tm_min = minute;
  tmUtc.tm_sec = second;
  time_t utcEpoch = timegm(&tmUtc);

  struct tm localTm;
  localtime_r(&utcEpoch, &localTm);

  strftime(dateBuf, dateBufSize, "%a %m/%d", &localTm);

  bool military = (strcmp(timeFormat, "24") == 0);
  if (military) {
    strftime(timeBuf, timeBufSize, "%H:%M", &localTm);
  } else {
    char raw[12];
    strftime(raw, sizeof(raw), "%I:%M %p", &localTm);
    const char* trimmed = (raw[0] == '0') ? raw + 1 : raw;
    strncpy(timeBuf, trimmed, timeBufSize - 1);
    timeBuf[timeBufSize - 1] = '\0';
  }
}

// Next 5 upcoming games, all on one screen (per explicit request) rather
// than cycling one game at a time like the roster cards - two lines per
// game: team/home-away/date/time at size2 since that's the primary
// "what and when" info, venue+TV at size1 below it since it's secondary
// detail and there's a lot of it to fit. Row height (32px) deliberately
// keeps all 5 rows clear of the clock box, which occupies the bottom-left
// corner (y 196-236, see renderClock()) - the 5th row's second line lands
// around y=184, a 12px margin above it.
void renderSchedule() {
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 6);
  tft.println("Next 5 Games");
  tft.drawFastHLine(8, 30, 304, FLYERS_ORANGE);
  tft.drawFastHLine(8, 31, 304, FLYERS_ORANGE);

  if (!haveSchedule) {
    tft.setTextSize(1);
    tft.setCursor(8, 40);
    tft.println("Waiting for data...");
    return;
  }

  const int ROW_HEIGHT = 32;
  int y = 38;
  for (JsonObject g : scheduleDoc["games"].as<JsonArray>()) {
    const char* opponent = g["opponent"] | "???";
    bool isHome = g["is_home"];
    const char* startUtc = g["start_utc"] | "";

    char dateBuf[12] = "";
    char timeBuf[10] = "";
    if (startUtc[0]) {
      formatGameDateTime(startUtc, dateBuf, sizeof(dateBuf), timeBuf, sizeof(timeBuf));
    }

    tft.setTextSize(2);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setCursor(8, y);
    char line1[32];
    snprintf(line1, sizeof(line1), "%s%-4s%s %s", isHome ? "vs " : "@  ", opponent, dateBuf, timeBuf);
    tft.println(line1);

    // TV networks joined with "/" - sent as an array rather than a
    // pre-joined string from the Python side so the device controls the
    // formatting here, not the publisher.
    char tvStr[36] = "";
    bool firstNet = true;
    for (JsonVariant net : g["tv"].as<JsonArray>()) {
      const char* n = net.as<const char*>();
      if (!n) continue;
      if (!firstNet) strncat(tvStr, "/", sizeof(tvStr) - strlen(tvStr) - 1);
      strncat(tvStr, n, sizeof(tvStr) - strlen(tvStr) - 1);
      firstNet = false;
    }

    const char* venue = g["venue"] | "";
    char line2[72];
    if (tvStr[0]) {
      snprintf(line2, sizeof(line2), "%s  TV: %s", venue, tvStr);
    } else {
      snprintf(line2, sizeof(line2), "%s", venue);
    }
    tft.setTextSize(1);
    tft.setCursor(8, y + 18);
    tft.println(line2);

    y += ROW_HEIGHT;
  }
}

// Bottom-left clock box, present on every screen (called once from
// render() on any screen change, and again every ~1s from loop() so it
// stays live without repainting the whole screen behind it - see
// loop()). configTzTime() in setup() keeps time(nullptr)/localtime_r()
// auto-adjusted for whichever zone got saved (see TZ_TABLE); time(nullptr)
// reads as the 1970 epoch until NTP has synced, so nothing is drawn until
// it looks like a real timestamp rather than show a nonsense "8:00 PM" at
// boot.
bool timeIsSynced() {
  return time(nullptr) > 1700000000;  // sometime after Nov 2023
}

void renderClock() {
  if (!timeIsSynced()) return;

  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buf[12];
  bool military = (strcmp(timeFormat, "24") == 0);
  if (military) {
    strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
  } else {
    strftime(buf, sizeof(buf), "%I:%M %p", &timeinfo);
  }
  // Leading-zero trim only makes sense for 12h hours (24h's "08:00" is
  // the correct/expected form, not a leading zero to strip).
  const char* text = (!military && buf[0] == '0') ? buf + 1 : buf;

  // Doubled from the original 68x20/size1 box per explicit request (it
  // read too small) - left edge (x0) and bottom edge (y0+h) both stay
  // exactly where they were, so w/h double by growing upward and
  // rightward instead of in every direction. Cursor offset and text size
  // scale by the same factor so the text stays proportioned inside it.
  const int x0 = 4, y0 = 196, w = 136, h = 40;
  tft.fillRect(x0, y0, w, h, TFT_WHITE);
  tft.drawRect(x0, y0, w, h, FLYERS_ORANGE);
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setCursor(x0 + 10, y0 + 12);
  tft.print(text);
}

// Same 17 named ice zones and (x, y) rink coordinates (0-100 scale, net
// at top) as card_image.py's ZONE_COORDS / dashboard_publish.py's
// ZONE_ORDER, in the exact same order - the "heat" payload field sends
// just an index into this table per zone rather than a name and two
// coordinates, since this end already has all three (see
// dashboard_publish.py's build_edge_extras for the payload-size math).
const uint8_t ZONE_X[17] = {50, 50, 35, 65, 50, 13, 87, 50, 23, 77, 11, 89, 19, 81, 50, 50, 50};
const uint8_t ZONE_Y[17] = { 9, 20, 19, 19, 32, 13, 13, 47, 37, 37, 51, 51, 68, 68, 72, 88, 98};

// This panel renders color565()'s R and B bytes swapped relative to what
// the library assumes (confirmed via FLYERS_ORANGE below - a hand-picked
// orange came out blue until its R/B bytes were fed in swapped). That fix
// only ever got applied to FLYERS_ORANGE itself - the heat map's old
// TFT_RED/TFT_YELLOW/TFT_BLUE percentile coloring used the library's
// plain (uncompensated) named constants, so "red" was almost certainly
// rendering blue-ish on the real screen. rgbColor() applies the same
// swap to any hand-picked hue so it actually comes out as intended here.
uint16_t rgbColor(uint8_t r, uint8_t g, uint8_t b) {
  return tft.color565(b, g, r);
}

// blue -> cyan -> green -> yellow -> red, t=0..1 (least to most shots) -
// a real heat gradient in place of the old three-color percentile
// coding, so the biggest/reddest dot is unambiguously "the most", not
// dependent on remembering what each of three fixed colors meant.
uint16_t heatColor(float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  uint8_t r, g, b;
  if (t < 0.25) {
    float u = t / 0.25;
    r = 0; g = (uint8_t)(u * 255); b = 255;
  } else if (t < 0.5) {
    float u = (t - 0.25) / 0.25;
    r = 0; g = 255; b = (uint8_t)((1 - u) * 255);
  } else if (t < 0.75) {
    float u = (t - 0.5) / 0.25;
    r = (uint8_t)(u * 255); g = 255; b = 0;
  } else {
    float u = (t - 0.75) / 0.25;
    r = 255; g = (uint8_t)((1 - u) * 255); b = 0;
  }
  return rgbColor(r, g, b);
}

// zx/zy on the 0-100 ZONE_X/ZONE_Y scale -> actual screen pixels within
// a box at (bx, by, bw, bh).
int zonePx(int zx, int bx, int bw) { return bx + (zx * bw) / 100; }
int zonePy(int zy, int by, int bh) { return by + (zy * bh) / 100; }

// Half-rink schematic (net at top, down through the neutral-zone edge) -
// boards, goal line, a goal (posts), the blue line, and the two
// offensive-zone faceoff circles/dots, all decorative/orientation-only
// (not data-driven) and drawn in Flyers orange per explicit request.
// Uses the same ZONE_X/ZONE_Y table the heat dots use (indices 0, 8-11)
// so the circles land exactly where "L Circle"/"R Circle" shots plot.
void renderRink(int x0, int y0, int w, int h) {
  int bx = x0 + 4, by = y0 + 4, bw = w - 8, bh = h - 8;
  tft.drawRoundRect(bx, by, bw, bh, 24, FLYERS_ORANGE);

  // Goal line, just past the net.
  int goalLineY = zonePy(15, by, bh);
  tft.drawFastHLine(zonePx(12, bx, bw), goalLineY, zonePx(88, bx, bw) - zonePx(12, bx, bw), FLYERS_ORANGE);

  // Net (goal posts) straddling the goal line.
  int netX = zonePx(42, bx, bw);
  int netY = zonePy(5, by, bh);
  int netW = zonePx(58, bx, bw) - netX;
  int netH = zonePy(15, by, bh) - netY;
  tft.drawRect(netX, netY, netW, netH, FLYERS_ORANGE);

  // Blue line.
  tft.drawFastHLine(bx, zonePy(ZONE_Y[12], by, bh), bw, FLYERS_ORANGE);

  // Offensive-zone faceoff circles (indices 8, 9 = L Circle, R Circle).
  int circleR = bw / 6;
  for (int idx = 8; idx <= 9; idx++) {
    int cx = zonePx(ZONE_X[idx], bx, bw);
    int cy = zonePy(ZONE_Y[idx], by, bh);
    tft.drawCircle(cx, cy, circleR, FLYERS_ORANGE);
    tft.fillCircle(cx, cy, 2, FLYERS_ORANGE);
  }

  // Neutral-zone faceoff dots (indices 10, 11 = Outside L, Outside R).
  tft.fillCircle(zonePx(ZONE_X[10], bx, bw), zonePy(ZONE_Y[10], by, bh), 3, FLYERS_ORANGE);
  tft.fillCircle(zonePx(ZONE_X[11], bx, bw), zonePy(ZONE_Y[11], by, bh), 3, FLYERS_ORANGE);
}

// One dot per zone this player has shot from, sized and colored by shot
// volume relative to their own max (not league-wide - this is "where
// THIS player shoots from", not an absolute-volume heat map).
void renderHeatDots(JsonArray heat, int x0, int y0, int w, int h) {
  int bx = x0 + 4, by = y0 + 4, bw = w - 8, bh = h - 8;

  int maxShots = 1;
  for (JsonArray entry : heat) {
    int shots = entry[1];
    if (shots > maxShots) maxShots = shots;
  }

  int maxRadius = bw / 11;
  for (JsonArray entry : heat) {
    int idx = entry[0];
    if (idx < 0 || idx > 16) continue;  // defensive - see build_edge_extras, shouldn't happen
    int shots = entry[1];

    int cx = zonePx(ZONE_X[idx], bx, bw);
    int cy = zonePy(ZONE_Y[idx], by, bh);
    int radius = 3 + (shots * (maxRadius - 3)) / maxShots;
    tft.fillCircle(cx, cy, radius, heatColor((float)shots / maxShots));
  }
}

void renderPlayerCard() {
  tft.fillScreen(TFT_WHITE);

  if (!haveRoster) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setCursor(8, 6);
    tft.println("Waiting for roster data...");
    return;
  }

  JsonArray players = rosterDoc["players"].as<JsonArray>();
  if (players.size() == 0) {
    return;
  }
  // rosterIndex is kept in range by advanceScreen() below, not here -
  // this just renders whatever index it's currently pointed at.
  JsonObject p = players[rosterIndex];

  const char* name = p["name"];
  const char* position = p["position"];
  int number = p["number"].is<int>() ? p["number"].as<int>() : 0;
  int age = p["age"].is<int>() ? p["age"].as<int>() : 0;

  // "toi" is already formatted as an M:SS string server-side (see
  // providers/nhl.py's get_compact_player_stats) - just display it, no
  // parsing needed on this end. Absent/older cached payloads (from before
  // this field existed) fall back to "n/a" via the "| default" idiom,
  // same as how the Pi side formats a genuinely missing value.
  const char* toi = p["toi"] | "n/a";

  // "First Last" -> two lines, name staying at the same (8, 4) spot it
  // always has, last name explicitly on its own line instead of relying
  // on println's auto-wrap. Splits on the first space; NHL rosters don't
  // currently have a multi-word first name, so nothing more elaborate is
  // needed here.
  char firstName[24] = "";
  char lastName[24] = "";
  const char* space = strchr(name, ' ');
  if (space) {
    size_t firstLen = space - name;
    if (firstLen >= sizeof(firstName)) firstLen = sizeof(firstName) - 1;
    memcpy(firstName, name, firstLen);
    firstName[firstLen] = '\0';
    strncpy(lastName, space + 1, sizeof(lastName) - 1);
    lastName[sizeof(lastName) - 1] = '\0';
  } else {
    strncpy(firstName, name, sizeof(firstName) - 1);
  }

  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setCursor(8, 4);
  tft.println(firstName);
  tft.setCursor(8, 26);
  tft.println(lastName);

  // Divider lines (name/stats, stats/bio), left column only so they
  // don't cut across the rink diagram's own top border for skaters -
  // 2px thick (two adjacent 1px lines) to read clearly at this size.
  const int DIVIDER_X0 = 4, DIVIDER_X1 = 156;
  tft.drawFastHLine(DIVIDER_X0, 46, DIVIDER_X1 - DIVIDER_X0, FLYERS_ORANGE);
  tft.drawFastHLine(DIVIDER_X0, 47, DIVIDER_X1 - DIVIDER_X0, FLYERS_ORANGE);

  // Two stat lines, values inline with a short label each (no separate
  // header row - at this text size a header row would cost as much
  // vertical space as a third stat line) - GP/TOI (participation) on
  // their own line per explicit request, G/A/P (performance) above it.
  // TOI goes unlabeled - squeezed against the rink diagram (only ~150px
  // before it starts), and its M:SS format is self-evident right next to GP.
  int gp = p["gp"], g = p["g"], a = p["a"], pts = p["p"];

  char line1[20], line2[20];
  snprintf(line1, sizeof(line1), "G%d A%d P%d", g, a, pts);
  snprintf(line2, sizeof(line2), "GP%d  %s", gp, toi);
  tft.setCursor(8, 54);
  tft.println(line1);
  tft.setCursor(8, 76);
  tft.println(line2);

  tft.drawFastHLine(DIVIDER_X0, 98, DIVIDER_X1 - DIVIDER_X0, FLYERS_ORANGE);
  tft.drawFastHLine(DIVIDER_X0, 99, DIVIDER_X1 - DIVIDER_X0, FLYERS_ORANGE);

  char bioLine[20];
  snprintf(bioLine, sizeof(bioLine), "#%d %s %d", number, position, age);
  tft.setCursor(8, 106);
  tft.println(bioLine);

  // Shot map: the entire right half of the screen. SCREEN_ROSTER only
  // ever indexes skaters now (see skaterCount()) - goalies get their own
  // table screen (renderGoalieTable()) instead, since they have no
  // shot-location Edge data to map anyway.
  renderRink(160, 0, 160, 240);
  if (p["heat"].is<JsonArray>()) {
    renderHeatDots(p["heat"].as<JsonArray>(), 160, 0, 160, 240);
  }
}

// All goalies at once (name, #, age, gp, sv%, gaa) instead of cycling
// them individually through renderPlayerCard() like skaters, per
// explicit request - SCREEN_ROSTER no longer indexes goalies at all
// (see skaterCount()), this table is the only place they appear.
void renderGoalieTable() {
  tft.fillScreen(TFT_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setCursor(8, 6);
  tft.println("Goalies");

  tft.drawFastHLine(8, 30, 304, FLYERS_ORANGE);
  tft.drawFastHLine(8, 31, 304, FLYERS_ORANGE);

  if (!haveRoster) {
    tft.setTextSize(1);
    tft.setCursor(8, 40);
    tft.println("Waiting for roster data...");
    return;
  }

  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  char hdr[40];
  snprintf(hdr, sizeof(hdr), "%-16s%-4s%-5s%-4s%-7s%-5s", "Name", "#", "Age", "GP", "SV%", "GAA");
  tft.setCursor(8, 40);
  tft.println(hdr);

  int y = 56;
  for (JsonObject p : rosterDoc["players"].as<JsonArray>()) {
    if (!p["w"].is<int>()) continue;  // skaters - this screen is goalies only

    const char* name = p["name"];
    int number = p["number"].is<int>() ? p["number"].as<int>() : 0;
    int age = p["age"].is<int>() ? p["age"].as<int>() : 0;
    int gp = p["gp"];
    float svp = p["svp"];
    float gaa = p["gaa"];

    char line[40];
    snprintf(line, sizeof(line), "%-16.16s%-4d%-5d%-4d%-7.3f%-5.2f", name, number, age, gp, svp, gaa);
    tft.setCursor(8, y);
    tft.println(line);
    y += 16;
  }
}

void render() {
  if (currentScreen == SCREEN_STANDINGS) {
    renderStandings();
  } else if (currentScreen == SCREEN_SCHEDULE) {
    renderSchedule();
  } else if (currentScreen == SCREEN_ROSTER) {
    renderPlayerCard();
  } else {
    renderGoalieTable();
  }
  renderClock();
}

// dashboard_publish.py's build_roster_payload() always appends goalies
// after forwards/defensemen, never interleaved - so "index of the first
// goalie" is the same as "count of skaters", no separate flag needed per
// entry. Used to keep SCREEN_ROSTER's cycle skater-only (goalies get
// their own table screen instead - see renderGoalieTable()).
int skaterCount() {
  JsonArray players = rosterDoc["players"].as<JsonArray>();
  int i = 0;
  for (JsonObject p : players) {
    if (p["w"].is<int>()) break;
    i++;
  }
  return i;
}

// --------------------------------------------------------------------
// Screen cycling - standings, then the schedule, then each skater in
// turn, then the goalie table, then back to standings. All local
// timing, no dependency on the Pi's publish schedule - see this file's
// header comment.
// --------------------------------------------------------------------

void advanceScreen() {
  unsigned long now = millis();

  if (currentScreen == SCREEN_STANDINGS) {
    if (now - screenChangedAt < STANDINGS_DURATION_MS) return;
    currentScreen = SCREEN_SCHEDULE;
    screenChangedAt = now;
    render();
    return;
  }

  if (currentScreen == SCREEN_SCHEDULE) {
    if (now - screenChangedAt < SCHEDULE_DURATION_MS) return;
    currentScreen = SCREEN_ROSTER;
    rosterIndex = 0;
    screenChangedAt = now;
    render();
    return;
  }

  if (currentScreen == SCREEN_ROSTER) {
    if (now - screenChangedAt < PLAYER_CARD_DURATION_MS) return;

    int total = haveRoster ? rosterDoc["players"].as<JsonArray>().size() : 0;
    int skaters = haveRoster ? skaterCount() : 0;
    rosterIndex++;
    if (rosterIndex >= skaters) {
      rosterIndex = 0;
      currentScreen = (skaters < total) ? SCREEN_GOALIES : SCREEN_STANDINGS;
    }
    screenChangedAt = now;
    render();
    return;
  }

  // SCREEN_GOALIES
  if (now - screenChangedAt < PLAYER_CARD_DURATION_MS) return;
  currentScreen = SCREEN_STANDINGS;
  rosterIndex = 0;
  screenChangedAt = now;
  render();
}

// --------------------------------------------------------------------
// MQTT - same connect/subscribe/reconnect shape as test_message_receiver.ino,
// just two topics and a JSON callback instead of one topic and a raw string.
// --------------------------------------------------------------------

void callback(char* topic, byte* payload, unsigned int length) {
  // setBufferSize() below must be >= the largest payload this ever
  // receives (roster was ~10.5KB once heat map data was added, standings
  // ~1KB, schedule a few hundred bytes - see bots/alerts/pollers/
  // dashboard_publish.py's own size check, run after any future field
  // additions) or PubSubClient silently drops anything over the buffer
  // instead of calling back with a truncated one.
  //
  // Explicit if/else per topic (not the old two-way ternary) now that
  // there are three - each one maps to its own doc, "have" flag, and set
  // of screens that should redraw immediately if this message arrives
  // while one of them is showing.
  JsonDocument* target;
  bool* haveFlag;
  bool isCurrentScreen;

  if (strcmp(topic, standingsTopic) == 0) {
    target = &standingsDoc;
    haveFlag = &haveStandings;
    isCurrentScreen = (currentScreen == SCREEN_STANDINGS);
  } else if (strcmp(topic, scheduleTopic) == 0) {
    target = &scheduleDoc;
    haveFlag = &haveSchedule;
    isCurrentScreen = (currentScreen == SCREEN_SCHEDULE);
  } else if (strcmp(topic, rosterTopic) == 0) {
    target = &rosterDoc;
    haveFlag = &haveRoster;
    isCurrentScreen = (currentScreen == SCREEN_ROSTER || currentScreen == SCREEN_GOALIES);
  } else {
    return;  // subscribed to something we don't otherwise handle - shouldn't happen
  }

  DeserializationError err = deserializeJson(*target, payload, length);
  if (err) {
    Serial.print("JSON parse failed for ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(err.c_str());
    return;
  }

  *haveFlag = true;

  // Redraw immediately only if this topic's screen is the one currently
  // showing - avoids interrupting whatever's on screen right now with an
  // update for a different screen.
  if (isCurrentScreen) {
    render();
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    if (client.connect("CYD-NHL-Dashboard", mqtt_username, mqtt_password)) {
      client.subscribe(standingsTopic);
      client.subscribe(rosterTopic);
      client.subscribe(scheduleTopic);
    } else {
      delay(2000);
    }
  }
}

// --------------------------------------------------------------------
// OTA - pull-based rather than the usual ArduinoOTA, since that only
// works from a machine on the same LAN as the device (see this file's
// Iteration 10 note up top). Checks GitHub's "latest release" API, and
// if its tag doesn't match FIRMWARE_VERSION, downloads and flashes
// whatever .bin is attached to that release. Every failure mode (no
// internet, no releases yet, rate-limited, a release with no .bin
// attached) just returns quietly - this runs unattended on someone
// else's desk, there's no one to show an error to, and the next
// scheduled check will just try again.
// --------------------------------------------------------------------

// True if `latest` (a GitHub release tag, "vMAJOR.MINOR.PATCH") is a
// newer version than `current` (FIRMWARE_VERSION, same format) - a plain
// string-inequality check (the original approach) treats ANY different
// tag as "newer", including older ones. Confirmed live: a device running
// v1.1.1 attempted to "update" itself to the still-published v1.1.0
// because the tags simply didn't match, which is exactly backwards.
bool isNewerVersion(const char* latest, const char* current) {
  if (latest[0] == 'v') latest++;
  if (current[0] == 'v') current++;
  int latestParts[3] = {0, 0, 0};
  int currentParts[3] = {0, 0, 0};
  sscanf(latest, "%d.%d.%d", &latestParts[0], &latestParts[1], &latestParts[2]);
  sscanf(current, "%d.%d.%d", &currentParts[0], &currentParts[1], &currentParts[2]);
  for (int i = 0; i < 3; i++) {
    if (latestParts[i] != currentParts[i]) return latestParts[i] > currentParts[i];
  }
  return false;  // identical version
}

void checkForOTAUpdate() {
  Serial.printf("[OTA] Checking for update (running %s)...\n", FIRMWARE_VERSION);

  WiFiClientSecure apiClient;
  apiClient.setInsecure();  // see espClient's setInsecure() comment in setup() - same tradeoff

  HTTPClient http;
  String apiUrl = String("https://api.github.com/repos/") + OTA_REPO + "/releases/latest";
  http.begin(apiClient, apiUrl);
  http.addHeader("User-Agent", "esp32-nhl-dashboard");  // GitHub's API rejects requests with no User-Agent at all
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[OTA] GitHub API request failed, HTTP code %d\n", httpCode);
    http.end();
    return;
  }

  JsonDocument releaseDoc;
  DeserializationError err = deserializeJson(releaseDoc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("[OTA] Failed to parse release JSON: %s\n", err.c_str());
    return;
  }

  const char* tagName = releaseDoc["tag_name"];
  Serial.printf("[OTA] Latest release tag: %s\n", tagName ? tagName : "(none)");
  if (!tagName || !isNewerVersion(tagName, FIRMWARE_VERSION)) {
    Serial.println("[OTA] Already up to date.");
    return;  // no tag, or the published release isn't newer than what's already running
  }

  // Take the first asset ending in ".bin" rather than assuming a fixed
  // filename - whatever gets uploaded when cutting the release.
  const char* downloadUrl = nullptr;
  for (JsonObject asset : releaseDoc["assets"].as<JsonArray>()) {
    const char* name = asset["name"];
    if (name && strstr(name, ".bin")) {
      downloadUrl = asset["browser_download_url"];
      break;
    }
  }
  if (!downloadUrl) {
    Serial.println("[OTA] Newer release found but it has no .bin asset - skipping.");
    return;
  }

  // GitHub's browser_download_url is a redirect (302) to a very long
  // (~900+ char), heavily percent-encoded Azure SAS-token URL on a
  // different host (release-assets.githubusercontent.com). Confirmed via
  // `curl` from a laptop that the asset itself downloads cleanly through
  // that redirect - but ESP32's HTTPClient/HTTPUpdate following it
  // *internally* failed fast with no usable error (getLastError()==0,
  // empty string) on real hardware, twice, immediately after the
  // earlier -104 fix. Rather than trust HTTPUpdate's own redirect
  // handling for the actual multi-hundred-KB download, resolve the
  // redirect ourselves first (a tiny, low-risk request whose only job is
  // reading the Location header as a full String, not a fixed buffer),
  // then hand HTTPUpdate the already-resolved final URL - so the real
  // download is a single clean connection to the CDN host from scratch,
  // never touching HTTPUpdate's redirect-following logic at all.
  // Scoped in its own block, deliberately - redirectClient/redirectHttp
  // (and their mbedTLS session memory, which is heavy on an ESP32) need
  // to be fully destructed before updateClient opens a second TLS
  // connection below. Confirmed live: without this scoping, the redirect
  // resolved fine (HTTP 302, correct Location) but the follow-up
  // download connection came back "HTTP error: connection refused"
  // (error -1) - two concurrent TLS sessions was one too many.
  String finalUrl = downloadUrl;
  int redirectCode = 0;
  {
    WiFiClientSecure redirectClient;
    redirectClient.setInsecure();
    HTTPClient redirectHttp;
    redirectHttp.begin(redirectClient, downloadUrl);
    redirectHttp.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    redirectHttp.addHeader("User-Agent", "esp32-nhl-dashboard");
    // HTTPClient discards every response header by default - header()
    // returns empty for anything not explicitly pre-registered here, no
    // matter what the server actually sent. Confirmed live: without
    // this, "Location" always came back empty even on a real 302
    // response.
    const char* headersToCollect[] = {"Location"};
    redirectHttp.collectHeaders(headersToCollect, 1);
    redirectCode = redirectHttp.GET();
    Serial.printf("[OTA] Redirect request returned HTTP %d\n", redirectCode);
    if (redirectCode == 301 || redirectCode == 302 || redirectCode == 303 ||
        redirectCode == 307 || redirectCode == 308) {
      finalUrl = redirectHttp.header("Location");
    }
    redirectHttp.end();
  }

  if (finalUrl.length() == 0) {
    Serial.println("[OTA] Redirect resolution failed - no Location header.");
    return;
  }
  Serial.printf("[OTA] Resolved redirect (%d), downloading %d-char URL, free heap %u\n",
                redirectCode, finalUrl.length(), ESP.getFreeHeap());

  // Iteration 29: learn the EXACT size first via its own short-lived,
  // scoped connection (same pattern as the redirect resolution above -
  // a GET whose body is never read, just closed after checking headers)
  // - UPDATE_SIZE_UNKNOWN (iterations 23/25) turned out to be the actual
  // root cause of iterations 27/28's failures: it makes Update expect
  // the FULL PARTITION size, not this image's real size, so writeStream()
  // has no way to know when to stop and eventually times out waiting for
  // data that will never come - and worse, that timeout's internal
  // _abort() call fully resets the update session (partition handle,
  // buffer, progress), not just an error flag, so there was no clean way
  // to recover after the fact (confirmed by reading _abort()'s actual
  // source - it calls _reset() before setting the error code). Knowing
  // the exact size up front avoids all of that.
  int contentLength = -1;
  {
    WiFiClientSecure peekClient;
    peekClient.setInsecure();
    HTTPClient peekHttp;
    peekHttp.begin(peekClient, finalUrl);
    peekHttp.addHeader("User-Agent", "esp32-nhl-dashboard");
    int peekCode = peekHttp.GET();
    contentLength = peekHttp.getSize();
    Serial.printf("[OTA] Size-check request returned HTTP %d, Content-Length %d\n", peekCode, contentLength);
    peekHttp.end();
    if (peekCode != HTTP_CODE_OK || contentLength <= 0) {
      Serial.println("[OTA] Size-check request didn't return a valid size - aborting.");
      return;
    }
  }

  // Ground truth instead of more guessing - the last theory (OTA
  // partition too small) looked solid on paper and was wrong even after
  // confirming a 1.9MB scheme and a full flash erase, so print exactly
  // what the chip itself sees before calling begin().
  Serial.printf("[OTA] Free sketch space: %u bytes\n", ESP.getFreeSketchSpace());
  const esp_partition_t* runningPartition = esp_ota_get_running_partition();
  const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(NULL);
  Serial.printf("[OTA] Running partition: %s @ 0x%x, size %u\n",
                runningPartition ? runningPartition->label : "(null)",
                runningPartition ? runningPartition->address : 0,
                runningPartition ? runningPartition->size : 0);
  Serial.printf("[OTA] Next update partition: %s @ 0x%x, size %u\n",
                nextPartition ? nextPartition->label : "(null)",
                nextPartition ? nextPartition->address : 0,
                nextPartition ? nextPartition->size : 0);

  // Called here - BEFORE the download's TLS connection is even opened -
  // deliberately (iteration 25). Verbose core logging (iteration 24)
  // showed Update.begin() failing with "_buffer allocation failed": this
  // board has no PSRAM, so WiFi/TLS/TFT_eSPI/WiFiManager/MQTT's 16KB
  // buffer all share one small internal SRAM pool, and by the time a
  // download's own TLS session was already open (needed to stream the
  // image), there wasn't one large enough contiguous block left for
  // Update's own buffer. The size-check connection above is already
  // closed by this point too, for the same reason. Now passes the exact
  // size learned above (iteration 29) instead of UPDATE_SIZE_UNKNOWN.
  if (!Update.begin(contentLength)) {
    Serial.printf("[OTA] Update.begin() failed (%s).\n", Update.errorString());
    return;
  }

  // Manual HTTPClient + Update flow, not httpUpdate.update() - that
  // wrapper gave nothing but "error 0, empty string" through three
  // different real bugs in a row (iterations 14/17/19), which stopped
  // being useful information. This way every step below either succeeds
  // visibly or prints a real reason.
  WiFiClientSecure updateClient;
  updateClient.setInsecure();
  updateClient.setTimeout(15000);  // a bit more headroom than the default for a ~1.2MB transfer
  HTTPClient updateHttp;
  updateHttp.begin(updateClient, finalUrl);
  updateHttp.addHeader("User-Agent", "esp32-nhl-dashboard");
  updateHttp.setTimeout(15000);
  int updateCode = updateHttp.GET();
  int downloadContentLength = updateHttp.getSize();
  Serial.printf("[OTA] Download request returned HTTP %d, Content-Length %d\n", updateCode, downloadContentLength);

  if (updateCode != HTTP_CODE_OK || downloadContentLength != contentLength) {
    // Update.begin() already succeeded above with no matching end() -
    // left as-is rather than adding abort-handling for what should be a
    // rare case (the download itself has proven reliable in every prior
    // round) - Update's state is per-boot, so the next check (this loop
    // or the next boot) starts clean regardless.
    Serial.println("[OTA] Download request didn't match the size already committed to - aborting.");
    updateHttp.end();
    return;
  }

  size_t written = Update.writeStream(*updateHttp.getStreamPtr());
  Serial.printf("[OTA] Wrote %u of %d bytes\n", written, contentLength);

  if (written != (size_t)contentLength) {
    Serial.println("[OTA] writeStream() didn't deliver the full image - aborting.");
    updateHttp.end();
    return;
  }

  if (!Update.end() || !Update.isFinished()) {
    Serial.printf("[OTA] Update.end() failed: %s\n", Update.errorString());
    updateHttp.end();
    return;
  }

  updateHttp.end();
  Serial.println("[OTA] Update applied successfully - rebooting.");
  ESP.restart();
}

// --------------------------------------------------------------------
// WiFiManager callbacks (see iteration 8 note up top).
// --------------------------------------------------------------------

// Fires when the portal's Save button is pressed - only happens if the
// portal was actually shown (no saved WiFi, or a saved network failed to
// connect). Every NHL team abbreviation is 3 letters, so entries are
// uppercased and clamped to that; a blank submission leaves whatever was
// already loaded/default alone rather than saving an empty team code.
// Team, time zone, and time format all save from this one callback -
// WiFiManager only supports a single setSaveParamsCallback, fired once
// when the portal's Save button is pressed (only happens if the portal
// was actually shown - no saved WiFi, or a saved network failed to
// connect). Each field is independently validated so a bad entry in one
// doesn't block the others from saving; blank/invalid falls back to
// whatever was already loaded rather than saving garbage.
void saveConfigParams() {
  String enteredTeam = teamParam->getValue();
  enteredTeam.trim();
  enteredTeam.toUpperCase();
  if (enteredTeam.length() > 0) {
    strncpy(teamCode, enteredTeam.c_str(), 3);
    teamCode[3] = '\0';
    prefs.putString("team", teamCode);
  }

  String enteredTz = tzParam->getValue();
  enteredTz.trim();
  enteredTz.toUpperCase();
  if (enteredTz.length() > 0 && posixTzFor(enteredTz.c_str()) != nullptr) {
    strncpy(tzAbbrev, enteredTz.c_str(), 3);
    tzAbbrev[3] = '\0';
    prefs.putString("tz", tzAbbrev);
  }

  String enteredFmt = timeFormatParam->getValue();
  enteredFmt.trim();
  strncpy(timeFormat, (enteredFmt == "24") ? "24" : "12", sizeof(timeFormat) - 1);
  timeFormat[sizeof(timeFormat) - 1] = '\0';
  prefs.putString("fmt", timeFormat);
}

// Fires the moment WiFiManager opens its own "ESP32-Dashboard-Setup" AP -
// without this the screen would just sit on "Connecting WiFi..." while
// actually waiting for a phone to join the AP and fill out the portal,
// which looks identical to "stuck" from the outside.
void configPortalStarted(WiFiManager* wmInstance) {
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.println("WiFi setup needed:");
  tft.setTextSize(1);
  tft.setCursor(10, 60);
  tft.println("1. Connect phone to WiFi");
  tft.setCursor(10, 76);
  tft.println("   \"ESP32-Dashboard-Setup\"");
  tft.setCursor(10, 96);
  tft.println("2. A setup page should open");
  tft.setCursor(10, 112);
  tft.println("   (or visit 192.168.4.1)");
  tft.setCursor(10, 128);
  tft.println("3. Pick your WiFi, team,");
  tft.setCursor(10, 144);
  tft.println("   time zone + time format");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(0);

  // Was tft.color565(0xF7, 0x49, 0x02) (Flyers orange, RGB order) and
  // rendered as blue instead. White/black/green text all looked correct
  // before this, but those three are the one set of colors that would
  // look "correct" whether or not red/blue are swapped (grayscale is
  // unaffected, and green isn't involved in an R/B swap) - so they never
  // actually proved the panel's color order matched what color565()
  // assumes, they just never disproved it either. A custom color with
  // meaningfully different R and B components (this one) was the first
  // real test, and it came back visibly wrong. Feeding the R and B bytes
  // in swapped is the standard fix for exactly this symptom on BGR-order
  // panels - if this is STILL not orange after reflashing, the swap
  // theory is wrong and it's something else (possibly invertDisplay()
  // behaving backwards on this specific panel batch).
  FLYERS_ORANGE = tft.color565(0x02, 0x49, 0xF7);

  // Load whatever team was saved on a previous setup (defaults to the
  // teamCode initializer, "PHI", the first time this ever runs on a
  // fresh device) - this also becomes the captive portal's pre-filled
  // value below, so re-running setup to change WiFi doesn't reset a
  // team that was already chosen.
  prefs.begin("dashboard", false);
  String savedTeam = prefs.getString("team", teamCode);
  savedTeam.trim();
  savedTeam.toUpperCase();
  if (savedTeam.length() > 0) {
    strncpy(teamCode, savedTeam.c_str(), 3);
    teamCode[3] = '\0';
  }

  String savedTz = prefs.getString("tz", tzAbbrev);
  savedTz.trim();
  savedTz.toUpperCase();
  if (posixTzFor(savedTz.c_str()) != nullptr) {
    strncpy(tzAbbrev, savedTz.c_str(), 3);
    tzAbbrev[3] = '\0';
  }

  String savedFmt = prefs.getString("fmt", timeFormat);
  savedFmt.trim();
  strncpy(timeFormat, (savedFmt == "24") ? "24" : "12", sizeof(timeFormat) - 1);
  timeFormat[sizeof(timeFormat) - 1] = '\0';

  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.println("Connecting WiFi...");

  teamParam = new WiFiManagerParameter("team", "Team Abbreviation (e.g. PHI)", teamCode, 4);
  tzParam = new WiFiManagerParameter("tz", "Time Zone (ET, CT, MT, PT, or AT)", tzAbbrev, 4);
  timeFormatParam = new WiFiManagerParameter("fmt", "Time Format (12 or 24)", timeFormat, 4);

  WiFiManager wm;
  wm.addParameter(teamParam);
  wm.addParameter(tzParam);
  wm.addParameter(timeFormatParam);
  wm.setSaveParamsCallback(saveConfigParams);
  wm.setAPCallback(configPortalStarted);
  // Give up and reboot (to try the whole thing again) rather than block
  // forever with the AP up and nobody around to connect to it.
  wm.setConfigPortalTimeout(180);

  bool connected = wm.autoConnect("ESP32-Dashboard-Setup");

  if (!connected) {
    tft.fillScreen(TFT_WHITE);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 20);
    tft.println("WiFi setup timed out");
    tft.setTextSize(1);
    tft.setCursor(10, 50);
    tft.println("Restarting to try again...");
    delay(3000);
    ESP.restart();
  }

  // Boot-status sequence (iteration 16, per explicit request) - redrawn
  // fresh here rather than reusing whatever's already on screen, so it
  // reads the same whether or not the captive portal was shown earlier
  // (which would otherwise have left its own instructions on screen
  // instead of "Connecting WiFi...").
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.println("Connecting WiFi...");
  tft.setCursor(10, 46);
  tft.println("Connecting to github...");

  // Runs here - BEFORE any MQTT setup - deliberately (iteration 26).
  // client.setBufferSize(16384) below is a permanent allocation held for
  // the rest of the sketch's life; on a no-PSRAM board already fighting
  // heap fragmentation to get checkForOTAUpdate() working at all
  // (iterations 24/25), there's no reason to have that 16KB already
  // carved out during the one moment memory is tightest. Once at boot,
  // so a device that's been off for a while grabs any pending release
  // before ever rendering a screen on stale firmware. loop() below
  // re-checks once a day after this. Reboots on its own if an update is
  // actually applied - the "Release"/"CROSBY SUCKS!!!" lines below only
  // ever show whatever version is left running after this call returns,
  // which is exactly the point of showing them.
  checkForOTAUpdate();
  lastOtaCheck = millis();

  tft.setCursor(10, 72);
  tft.print("Release ");
  tft.println(FIRMWARE_VERSION);
  tft.setCursor(10, 98);
  tft.println("CROSBY SUCKS!!!");
  delay(2000);

  // Built once here from whatever team ended up saved (either loaded
  // above, or just written by saveConfigParams() if the portal was
  // shown) - reconnectMQTT()/callback() below just reference these,
  // never rebuild them, so a team change mid-runtime isn't possible
  // without a reboot.
  snprintf(standingsTopic, sizeof(standingsTopic), "dashboard/%s/standings", teamCode);
  snprintf(rosterTopic, sizeof(rosterTopic), "dashboard/%s/roster", teamCode);
  snprintf(scheduleTopic, sizeof(scheduleTopic), "dashboard/%s/schedule", teamCode);

  // Encrypts the connection without pinning/bundling HiveMQ Cloud's CA
  // certificate on-device - a reasonable tradeoff for a hobby display,
  // not something to reuse anywhere handling sensitive data.
  espClient.setInsecure();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(16384);  // see callback()'s comment - must fit the largest payload, with headroom

  // Whichever zone ended up saved (auto DST-adjusted, see the POSIX TZ
  // strings in TZ_TABLE) for the bottom-left clock box on every screen -
  // see renderClock(). Independent of the Pi/MQTT side entirely, just
  // needs this network's own internet access to reach an NTP server.
  // posixTzFor() can't actually return nullptr here - tzAbbrev only ever
  // holds a value that already passed that same lookup, above or in
  // saveConfigParams() - but the "ET" fallback keeps this from silently
  // passing nullptr to configTzTime() if that invariant ever breaks.
  const char* posixTz = posixTzFor(tzAbbrev);
  configTzTime(posixTz ? posixTz : posixTzFor("ET"), "pool.ntp.org", "time.nist.gov");

  reconnectMQTT();

  screenChangedAt = millis();
  render();
}

void loop() {
  // Reconnect using the already-saved credentials on a transient drop -
  // previously unnecessary on a single stable home network, but friends'
  // networks are out of our control. Doesn't reopen the config portal;
  // that only happens from setup() if a saved network can't be reached
  // at boot.
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(500);
    return;
  }

  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  advanceScreen();

  // Redraw just the clock box every ~1s, independent of whatever screen
  // is showing - advanceScreen()/render() only repaint the clock as a
  // side effect of a full screen change (every 15s) or new MQTT data,
  // which would otherwise leave it up to 15s stale.
  static unsigned long lastClockTick = 0;
  unsigned long now = millis();
  if (now - lastClockTick >= 1000) {
    lastClockTick = now;
    renderClock();
  }

  // Daily OTA re-check (see checkForOTAUpdate()'s own comment) - the
  // initial one already happened in setup(). A successful update reboots
  // the device on its own; this only returns on "nothing to do."
  if (now - lastOtaCheck >= OTA_CHECK_INTERVAL_MS) {
    lastOtaCheck = now;
    checkForOTAUpdate();
  }
}
