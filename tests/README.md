# Host regression tests

Run the dependency-free C++11 model and radio lifecycle suites from the repository root:

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
