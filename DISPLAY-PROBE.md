# Guest display proof of concept

Status: source prepared; not compiled or tested on an iPhone. This is NOT a
completed second-screen implementation. No remote input or audio is implemented.

Upstream baseline: 9108ae624ea001f7d7696d48407f890b48fa2653 (LiveContainer 3.8.11).

## Design and limits

Each multitasking guest runs in a LiveProcess extension. TweakLoader installs
an opt-in three-finger long-press gesture inside that guest. Activating it starts
a small HTTP listener bound to the phone's Wi-Fi IPv4 address, with an ephemeral
port and random per-session URL token. The browser page polls JPEG frames at
roughly 2 fps. The capture happens inside the guest's UIKit window, not in the
LiveContainer host and not through whole-screen recording or AirPlay.

This avoids the host's remote scene surface, which ordinary host view snapshots
may fail to capture. However drawViewHierarchy can still miss WKWebView, GPU or
protected content. Minimised guest rendering, timer scheduling, and sandbox
network permissions are unproven. A changing red capture stamp alone proves
only that the timer runs; it does not prove the guest UI is still rendering.

LiveContainer must remain foreground on the phone for this experiment. The
second phone app must be another LiveContainer guest. Switching to an arbitrary
Home Screen app or locking the phone is outside this prototype's scope.

## Build

Use a fork under your GitHub account; the supplied workflow creates artifacts,
not a public release. Run `Build guest display probe` from the branch containing
these changes. It uses the upstream Xcode version and packaging script. The
packaging script fetches the upstream SideStore nightly and dylibify, so those
dependencies are not fully reproducible. Never put Apple account credentials or
signing certificates in Actions. Download and sign the resulting IPA locally
using iLoader, as with upstream LiveContainer. A successful build is not proof
of on-device functionality.

## Device acceptance test

1. Keep your original IPA available for rollback. Install the built combined
   LiveContainer+SideStore IPA using your existing Apple account and bundle ID.
   Repeat certificate import/diagnostics if signing changes.
2. Run iSH in Multitask mode. In its terminal run:

       while true; do date; sleep 1; done

3. Hold three fingers inside the iSH window for 1.2 seconds. The alert shows a
   session URL. Open that URL in a Windows browser on the same network, then
   dismiss the alert. Allow local-network access if prompted.
4. Confirm the terminal's dates change on the PC. Then minimise THIS iSH window
   using LiveContainer's yellow minus, leaving its process running.
5. Open the second iSH container on the phone. Type `echo PHONE ONLY` there.
   PASS: the PC continues showing changing dates from the first iSH while the
   phone shows the second terminal, and PHONE ONLY never appears on the PC.
   FAIL: black frames, frozen dates, entire-phone capture, or the first guest
   terminates. Record which outcome occurred; don't alter unrelated settings.
6. Restore the first iSH window and hold three fingers again to stop sharing.
   The session URL should cease responding. Killing that guest also stops it.

## Security and resource boundaries

Sharing is off by default. Only the selected guest's normal-level UIWindow is
captured. No login, credentials, files or touch events are accepted from clients.
The random URL is a bearer token; anyone with it on the LAN can view this guest.
HTTP is unencrypted, so use a trusted LAN and a non-sensitive test app. There is
no cloud relay or persistent pairing. Requests have bounded size and socket
timeouts. Port forwarding is neither required nor part of the design.

## Next milestone, only after acceptance passes

Implement guest-specific keyboard/touch forwarding with explicit pairing and
input consent. UIKit view capture does not itself provide remote input; that
requires a separate feasibility test. Improve frame rate and encoding later.
