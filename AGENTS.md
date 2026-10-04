# Project workflow

Before every merge into the default branch:

- Build the final application and run it interactively.
- Capture fresh screenshots and an animated GIF from that exact build. Show the changed behavior and representative channels; include live clock or noise animation, channel switching, and volume/mute when relevant.
- Store the optimized previews in `docs/media/` and embed them near the top of `README.md` so they appear on the GitHub repository home page. Replace the current previews rather than accumulating obsolete recordings.
- Inspect the exported screenshots and GIF. Keep the capture limited to the app, without unrelated windows or private desktop content. Cropping and resizing are allowed; do not fabricate application output.
- Include the media and README changes in the same branch/PR as the application changes. Include preview links and build/manual validation in the PR description.
- Do not merge without the refreshed previews. If capture is unavailable, report the concrete limitation to the user.

Keep raw capture frames and temporary builds under the ignored `chart-check/` directory. The preview packaging recipe is documented in `docs/media/README.md`.
