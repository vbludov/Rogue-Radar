# Host regression tests

Run the dependency-free C++11 tracker model, radio lifecycle, and shared scan
session suites from the repository root:

```sh
sh tests/run_host_tests.sh
```

The script uses `CXX` when set, defaults to `g++`, and builds in a temporary directory outside the checkout.

It also tests CC1101 power control with an injected register bus: USB rejection
without writes, identity/read/write failures, delayed battery cutoff, and
preservation of unrelated PMU settings across all register values. Physical
shutdown and PWR/QON wake still require the board.

The optional LVGL lifecycle regression compiles the pinned LVGL source with
the firmware's 64 KB built-in memory pool. Pass the installed LVGL dependency
directory explicitly:

```sh
bash tests/run_lvgl_tests.sh /path/to/.pio/libdeps/t_embed_cc1101/lvgl
```

It validates deferred deletion across animated screen transitions and external
deletion/address reuse. The lifecycle case uses a real encoder input device,
editing group, disabled button matrix, group handoff, animated AP-list return,
and deferred keyboard deletion for Esc and the shared top-Back cancel boundary
(not its GPIO interrupt/debounce). It also verifies interrupted keyboard loads
clear previous-screen state and idle-home retirement clears screen references
before menu recreation. It also exercises a representative 64-bit host version
of the Connect-to-AP cancel allocation pattern with 30 AP rows: reusing the
retained list must pass, while rebuilding a second list while the keyboard is
retained must hit the 64 KB allocator assertion. This is not a build of the
whole Arduino sketch, and exact byte counts can vary by host architecture.

The same LVGL suite exercises `scan_session_ui.h` with delayed radio cleanup,
timed expiry, continuous operation, Stop/Back, detail suspension and resume,
held encoder navigation, failed-start recovery, and unexpected screen deletion.
It also models deferred family-menu recreation after scan Back: the next tool
may open only after the expected family menu is active and its screen animation
has settled, rather than merely observing a detached session and no previous
screen.
The dependency-free suites also cover bounded alert de-duplication, paused
session timing, and delayed BLE scan-completion events across radio handoffs.

For an automated device lifecycle check, build the CC1101 profile with
`-DROGUE_RADAR_SCAN_SESSION_DEVICE_TEST=1`. This enables
`scan_session_device_test.h`, which visits all 20 supported scanner pages in
timed and continuous modes, exercises detail/tracker handoffs and active Back,
then runs 90-second BLE and Hybrid soaks. It prints `[ScanDiag]` status and
memory metrics over serial. Mode overrides are transient and do not write
saved preferences. Leave the controls untouched during the run, and restore
the normal build afterward. This diagnostic checks lifecycle behavior, not
the accuracy of every detector against known RF fixtures.

The host runner also exercises the SD record store and interrupted-write
recovery, bounded Nearby ranking/history, near/away/near learning evidence,
neighbor ambiguity, explicit exclusions, and discovery radio handoffs.

`-DROGUE_RADAR_KNOWN_DEVICE_TEST=1` enables the separate physical-board
diagnostic in `known_devices_device_test.h`. It creates and deletes only its
own temporary SD record, then accepts serial commands to exercise the real
Nearby, Learn, Saved, and Track pages. `status` reports candidates, learning
samples and memory; `select N`, `learn`, `capture`, and `next` operate the capture stages.
Movement must be performed by a person between stages. The fixture-specific
`save-myamazfit` command is only for the user's authorized Amazfit test and
requires a consistent completed capture. Restore normal firmware afterward;
release builds expose none of these commands. Simulated signal tests do not
establish a real-world identification success rate.
