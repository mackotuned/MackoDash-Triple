# Left dashboard source

The editable Claude source is retained in this directory:

- `make_side.py` regenerates the background, preview, and layout metadata.
- `side_480_round.png` is the clean LVGL background.
- `side_480_round_preview.png` shows the intended completed screen.
- `side_layout.json` is the overlay geometry contract.
- `SIDE_SCREEN_NOTES.md` documents the value and arc mappings.

Run `python make_side.py` from this directory after changing the artwork, then
regenerate `../side_480_rgb565.bin` from the clean 480x480 PNG before building.