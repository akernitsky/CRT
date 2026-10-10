# Updating the project previews

Capture the final running application before every merge, as required by the root `AGENTS.md`. Use actual window screenshots or a window recording. Keep raw capture frames in the ignored `chart-check/news-frames/` folder; avoid capturing other applications.

The current GIF is an edited sequence of real application captures, not a simulated render. It is silent and sampled at a lower frame rate than the app; pauses between demonstrations are shortened.

The channel 11/12 capture set includes both news presenter styles, the furnished room, independent inner-TV power off/on, channel switching, moving static and clock hands, volume and mute, plus the original full-window broadcasts. `capture.json` records the SHA-256 of the Release x64 executable used. The crop removes window chrome and the pointer highlight near the top edge. Raw frames remain under `chart-check/news-frames/`.

Update `capture.json` to reference the new raw frames. Its `crop` rectangle removes window chrome and excess side margins. `screenshots` maps output PNG names to captured frames, and `animation` lists the GIF sequence with durations in milliseconds. Check the actual contents of every selected frame, particularly if the user interacted during capture.

With Python and Pillow available, package the images from the repository root:

```powershell
python scripts/make_preview.py chart-check/news-frames docs/media/capture.json
```

Inspect the PNG files and the animation, verify that the README links resolve, and commit the refreshed previews with the application change. Raw captures are intentionally not committed; the recipe requires a fresh capture session.
