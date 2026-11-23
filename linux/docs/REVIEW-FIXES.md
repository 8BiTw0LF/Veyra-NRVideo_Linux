# Fixes from the code review (VeyAmux 1.8)

On 2026-11-22, a code review was posted as issues 1–13 in
[MarkStokes-outlook/VeyAmux](https://github.com/MarkStokes-outlook/VeyAmux/issues). Each finding was checked against
the code. This page says what was fixed in 1.8, why, and what is left. (Issue 9 could not be read while this was
written. GitHub kept refusing the page.)

| Issue | Status |
|---|---|
| [#1](https://github.com/MarkStokes-outlook/VeyAmux/issues/1) V4L2 buffers through the Vulkan upload | Fixed |
| [#2](https://github.com/MarkStokes-outlook/VeyAmux/issues/2) Plane bounds and MJPEG padding | Fixed |
| [#3](https://github.com/MarkStokes-outlook/VeyAmux/issues/3) Range, transfer and primaries | Partly: the card's YUV range |
| [#4](https://github.com/MarkStokes-outlook/VeyAmux/issues/4) Recording resolution independent of the preview | Fixed |
| [#5](https://github.com/MarkStokes-outlook/VeyAmux/issues/5) OBS frame access and shutdown | Fixed |
| [#6](https://github.com/MarkStokes-outlook/VeyAmux/issues/6) Build provenance and update installation | Fixed |
| [#7](https://github.com/MarkStokes-outlook/VeyAmux/issues/7) Credentials | Fixed (file permissions) |
| [#8](https://github.com/MarkStokes-outlook/VeyAmux/issues/8) Media I/O and background work on shutdown | Mostly fixed |
| [#9](https://github.com/MarkStokes-outlook/VeyAmux/issues/9) Linux tests and a GPU baseline | Started: a live-path test runs in CI |
| [#10](https://github.com/MarkStokes-outlook/VeyAmux/issues/10) Vulkan memory and resize stalls | Fixed: resize stalls |
| [#11](https://github.com/MarkStokes-outlook/VeyAmux/issues/11) Temporal resets and presentation cadence | Partly |
| [#12](https://github.com/MarkStokes-outlook/VeyAmux/issues/12) HDR precision and frame-generation transfer | Partly: the transfer |
| [#13](https://github.com/MarkStokes-outlook/VeyAmux/issues/13) Profiling | Not yet (needs measurements on real hardware) |

## #1 Keep V4L2 capture buffers owned through the Vulkan upload — fixed
**What was wrong:** for uncompressed capture (NV12, YUYV, UYVY, P010), VeyAmux reads the picture straight from the
driver's buffer and does not copy it first. The buffer was meant to stay with VeyAmux until the renderer had copied
it, through a `shared_ptr` with a custom deleter. But that `shared_ptr` held a null pointer, and
`if (!out.hold) QBUF` treated it as empty. So the buffer was handed back to the driver at once, and again when the
frame was dropped. The card could write the next picture into a buffer the renderer was still reading. The result
could be torn frames, frames in the wrong order, and wrong repeat detection for frame generation.

**What changed:** every dequeued buffer has one owner, a lease that queues it again exactly once, when the last frame
holding it lets go. The card always keeps at least three buffers to capture into. If the renderer holds too many, the
picture is copied instead. VeyAmux now asks for 8 buffers instead of 6. The renderer also lets go at once of a
picture it will not draw again, and of everything when the live path stops.

## #2 Validate capture plane bounds and pad MJPEG packets — fixed
**What was wrong:** MJPEG packets were handed to FFmpeg straight from the driver's buffer. FFmpeg may read up to 64
bytes past the end of a packet, and that buffer has no padding there. Raw frames were also not checked against the
size of all their planes. The GPU upload buffer was always `w × h × 4` bytes, which can be too small for a card that
pads its rows.

**What changed:** the JPEG is copied into a buffer with FFmpeg's zero padding. A frame shorter than its planes is
refused before anything reads it, both on the CPU and before the GPU copy. Odd sizes and too-short strides are
refused for YUV. The upload buffer grows to the frame's real size.

## #3 Preserve range, transfer and primaries — partly
**What changed:** when the card says its YUV is full range (V4L2 quantization), the picture is now decoded as full
range on the GPU path too. Before, the GPU path always decoded limited range. Carrying the whole colour description
(matrix, primaries, transfer) through every path is a larger change and is not done yet. Note: the colour report in
[8BiTw0LF/VeyAmux#2](https://github.com/8BiTw0LF/VeyAmux/issues/2) points at the experimental HDR output, not this.

## #4 Keep recording and stream resolution independent of the preview — fixed
**What was wrong:** recording, instant replay and streaming used the picture after it was fitted to the window. If you
resized the window or moved it to a screen with another scale, the encoder got a new size, and that ended the
recording and the stream. Write errors (a full disk) and a failed file ending were ignored, so "Recording saved" could
appear for a broken file.

**What changed:** recording and streaming take the picture before it is fitted to the window. A write or file-ending
error stops the recording with a message. A replay shows "saved" only when the file was finished correctly.

## #5 Restrict OBS frame access and make shutdown safe — fixed
**What was wrong:** the local OBS server sent `Access-Control-Allow-Origin: *` on every response, so any web page open
in a browser could read the live picture from `127.0.0.1`. A site that points its own name at 127.0.0.1 (DNS rebinding)
could also do so. `stop()` aborted the sockets while it walked the list of viewers, and an abort removes a viewer from
that list straight away.

**What changed:**
- Only `/ping` (which the saved browser file needs) can be read by other pages. The pictures carry no CORS header, so a
  page can show them but never read their pixels.
- Requests with a foreign `Host` are refused.
- At most 8 viewers. Connections that never send a request are closed after 10 s.
- A picture from before a stop is never sent again.
- `stop()` takes the list out first.

The OBS browser file keeps working unchanged.

## #6 Harden AppImage build provenance and update installation — fixed
**What was wrong:** the build downloaded linuxdeploy, its Qt plugin and appimagetool from "continuous" releases, and
the newest DXC. appimagetool also fetched the newest AppImage runtime while building. None of them was checked. The
GitHub Actions were pinned by tag only.

**What changed:**
- All of these tools, the runtime included, are fixed releases checked by SHA-256. A mismatch stops the build.
- The workflow's actions are pinned to commits.
- An in-app update must come from GitHub (after redirects), have the size GitHub lists and be a real type-2 AppImage,
  in addition to the checksum it was already checked against.

## #7 Protect streaming and Remote Play credentials — fixed (file permissions)
**What changed:** the stream key, the PSN account ID and the console registration keys are kept in
`~/.config/VeyAmux/VeyAmux.conf`. That file is now readable by your user only (0600, folder 0700); Qt keeps the
permissions when it rewrites the file. A keyring (Secret Service) is a possible next step.

## #8 Bound media I/O and cancel background work safely on shutdown — mostly fixed
**What was wrong:** a stalled read (a network share or stream) could not be interrupted when the player closed.
Background threads (update check and download, lsfg-vk download, Remote Play) checked a "closing" flag and then posted
to the UI. VeyAmux could finish closing between those two steps.

**What changed:**
- FFmpeg has an interrupt callback tied to closing.
- Local files may only read local files: a playlist inside one cannot reach out to the network.
- Network reads time out after 15 s.
- Background results go to the UI through a gate. VeyAmux closes the gate under a lock before it is destroyed, so a
  result can never reach a closed window.

Not done: opening a file off the UI thread.

## #10 Reduce Vulkan working memory and prevent preview resize stalls — resize stalls fixed
**What was wrong:** any change of window size rebuilt the whole enhancement chain after `vkDeviceWaitIdle`. That
stalled the GPU and threw away the history of the denoiser, anti-flicker and upscaler.

**What changed:** when only the window size changes, only the display images are made again. The old ones are freed
a few frames later, when no frame in flight can use them, and there is no device-wide wait. Smaller working memory is
not done yet.

## #11 Reset temporal processing reliably — partly
**What changed:**
- A seek reset no longer gets lost when a newer frame replaces the one that carried it.
- A reset now also restarts frame generation and the HUD lock.
- A new picture is drawn at once: Qt no longer waits for the compositor's frame callback.
- `VEYAMUX_DEBUG_PACE=1` logs where each picture's time goes.

Not done: measuring the real time the picture reaches the screen, and showing it apart from the drawing times.

## #12 HDR precision and the frame-generation transfer — the transfer
**What changed:** with HDR output, FSR frame generation is now told that the frames are PQ-coded, with their peak.
Before, it was told sRGB. A 16-bit preview path is not done yet.

## #9 and #13
A test of the live capture path (`veyamux-live-test`) now runs in CI on software Vulkan. It covers window resizes,
padded rows and short frames, and it passes Vulkan validation. The performance work in #13 waits for measurements on
real hardware.
