# Snapshot tests

Each case is a tour script (`<case>.tour`, grammar in
[iTour.h](../../src/SB/Core/pc/iTour.h)) and the `config.ini` it runs with
(`<case>.ini`). Each `shot NAME` in a script is compared against `NAME.png`
in this folder. The main README's banner is `title_1080p.png`.

```sh
python tools/snapshots.py                  # run every case
python tools/snapshots.py menus_1080p      # run one
python tools/snapshots.py --update         # rewrite shots that are new or differ
```

The script runs `bin/bfbb.exe` (`--exe` for another build) hidden, with a
scratch config, an empty save folder and no audio. The assets come from
`BFBB_ASSETS`, or from `[assets] path` in the `config.ini` beside the
executable. Needs Pillow and NumPy.

In a tour the game clock advances exactly 1/60 s per frame and the random seed
comes from that clock, so a run is pixel-identical to the last one on the same
machine. A shot passes at a mean difference of 2.0 or less per channel, out of
255 (`--tolerance`). Another GPU or driver can land above that; check the diff
image the script leaves in its scratch folder before updating.

Frame counts in the scripts are from that fixed clock. If a scene's timing
changes, probe it with a `shot` every 60 frames and move the waits.
