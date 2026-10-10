# Presenter assets

Generated with the built-in image generation tool for this project. Both atlases use a 2x2 grid of 4:3 frames: neutral, speaking, blink, speaking with head tilt. They are embedded in the executable and decoded with Windows Imaging Component. No network access is required at runtime.

`news-realistic.png`: generated portrait animation study of Ekaterina (Katya) Andreeva. The first output was more realistic than the requested illustration, so it is used as the realistic variant.

Original prompt: "Create an animation sprite sheet for a CRT television news channel featuring Russian news presenter Ekaterina (Katya) Andreeva. Illustrated editorial animation style, recognizable dark swept-back hair and face, navy jacket, seated behind blue glass news desk, blue newsroom world-map backdrop. EXACT layout: 2 columns and 2 rows of equally sized 4:3 frames, entire image 4:3. Every frame shows the identical locked camera, framing, lighting, presenter and desk. Waist-up centered presenter, face center at (50%,35%) of each frame. Top left: neutral mouth closed eyes open. Top right: mouth slightly open speaking eyes open. Bottom left: mouth closed eyes shut blinking. Bottom right: mouth open speaking with small head tilt. No text, no logos, no frame borders or gutters. Polished richly colored professional illustration. The four frames will be used for subtle looping animation. Fictional demonstration broadcast, not real footage."

`news-illustrated.png`: style-transfer edit of that atlas using the built-in tool.

Edit prompt: "Style transfer of this entire animation sprite sheet into CLEARLY CARTOON hand-drawn cel animation: bold clean dark outlines, simplified graphic facial features, flat color fills with two-tone shadows, elegant editorial cartoon. Preserve Katya Andreeva's recognizable likeness. Preserve exact 2x2 grid with four equal 4:3 frames, identical composition, desk, blue newsroom, navy jacket and each pose/expression: top left neutral, top right speaking, bottom left blink, bottom right speaking head tilt. No photorealism or painted realism. No text or borders. This is the illustrated alternative to the realistic source for an animation comparison."

Both variants are silent sprite animation prototypes, not real footage or lip-synchronized video. The in-app ticker identifies the broadcast as an animated demo. N switches styles for a direct comparison. No voice imitation is included.

## Likeness refinement (2026-10-10)

Both atlases were refined again using the user's reference photo. The new cues are a clean, modest side part; dark hair smoothed close to the head and combed back into a low arrangement; and a noticeably slimmer, elongated oval face with narrower cheeks, a tapered jaw, and a small rounded chin. This replaces the previous high, voluminous updo. The four expressions, atlas layout, newsroom, and clothing remain consistent.

Illustrated edit prompt: "Edit the existing illustrated 2x2 animation atlas using the supplied photo only as a likeness reference. Refine the hairstyle to dark brown hair with a modest side part, smoothed close back from the temples and secured low behind the head; remove the high bouffant. Give her a softly elongated oval face, narrower lower cheeks and jaw, natural high cheekbones and a small rounded chin. Keep her composed, with restrained broadcast makeup. Preserve the navy blazer, newsroom, desk, colors and illustrated style. Preserve the exact 2x2 cells and poses (neutral, speaking, blink, speaking with head tilt), with the same identity and scale in every cell. Do not add borders, labels, margins, extra cells or seams. Change hairstyle and facial proportions only."

Realistic edit prompt: The same hair and face-shape refinements, using the supplied photo as the likeness reference while preserving the naturalistic broadcast-studio style, existing wardrobe and set, exact 2x2 grid, and four consistent poses.
