## VeyAmux-V.2.0.1

### Fix
- **Less delay on a console's dashboard and menus.** A picture that hardly moves (the PS5 dashboard, a pause screen)
  only gets a few new pictures a second. *Even out frame pacing* still held each of them for an even rhythm, so the
  delay figure rose to about 60 ms and the menu felt slow. Pictures that hardly move (under 12 new pictures a second) now go straight through; in a
  game the pacing works as before.
- The delay figure says when it comes from a still picture (it is then based on only a few pictures).

Everything new in 2.0 (AMD FSR 4 with FP8 on RDNA 4, AMD Anti-Lag / NVIDIA Reflex, and more) is in the
[2.0 release notes](https://github.com/8BiTw0LF/VeyAmux/releases/tag/VeyAmux-V.2.0).

### Download
Already on 1.6 or newer? VeyAmux offers the update by itself (on the start screen and under *System*). Otherwise,
download `VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it.

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them. FSR 4 is AMD's (MIT); the add-on is an unofficial build and not from AMD.
