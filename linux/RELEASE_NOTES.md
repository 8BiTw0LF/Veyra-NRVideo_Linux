## VeyAmux-V.1.7

### PlayStation Remote Play: add a console by IP address
Some consoles were never found, so they could not be registered. VeyAmux only looked for consoles by asking the whole
network, and that does not work when:
- the console is on another subnet or behind a VPN,
- the router keeps Wi-Fi devices apart (Wi-Fi isolation),
- a firewall drops the console's answer. Bazzite and Fedora have firewalld turned on, and it does not see an answer to
  a network-wide question as a reply.

Now the Remote Play window has **Add a console by IP address**. Type the address (on the console: *Settings → Network
→ Connection Status → View Connection Status*), choose PS5 or PS4 and press **Add**:
- VeyAmux asks the console directly at that address. That answer gets through the firewall, so the console shows up
  with its name and whether it is on or in rest mode.
- If it still does not answer, it can be registered straight at the address with the PIN. Play, wake-up and the rest
  then work as usual.
- An address typed by mistake can be removed again.

### Download
Already on 1.6? VeyAmux offers the update by itself (on the start screen and under *System*). Otherwise, download
`VeyAmux-x86_64.AppImage` below, make it executable (`chmod +x`) and run it.

Lossless Scaling frame generation still needs your own copy of Lossless Scaling (Steam) and lsfg-vk. VeyAmux does not
include either of them.
