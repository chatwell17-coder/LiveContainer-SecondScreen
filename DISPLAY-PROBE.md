# Independent guest display prototype

Baseline: LiveContainer 3.8.11 (9108ae624ea001f7d7696d48407f890b48fa2653).

## Verified on the iPhone

The selected iSH guest streams its own window to Windows at about two frames per
second. It continues updating minimised while another guest is used on the phone.
Text with spaces and literal plus signs, Send + Enter, Enter, and Backspace passed
on-device checks. This captures the selected guest, not the whole phone screen.

## Experimental touch update: requires device validation

Mouse down/move/up become one synthetic touch inside this guest. Private UIKit
selectors and locally constructed HID objects follow the in-process approach
used by KIF. No system-wide input dispatch or accessibility settings are changed.
Missing selectors/symbols return HTTP 409. Browser focus loss cancels the gesture;
an idle touch cancels after three seconds. SwiftUI hit testing uses the private
_UIHitTestContext when available. Dispatch success does not prove an app action.

Use Show/hide touch test in the PC viewer. Click and drag in the blue area and
move its slider. Both the label and slider must visibly respond. Hide the panel
before testing a real graphical app. Untick Experimental touch for basic button
activation. Wheel scrolling still directly adjusts a hit UIScrollView.

Multi-touch, audio, GPU/protected video capture, and arbitrary Home Screen apps
are not implemented. LiveContainer must remain open; the second phone app must
be another LiveContainer guest. Updating the red counter alone is not evidence
that app content continues rendering.

## Build and packaging

The manual Build guest display probe workflow exports GuestDisplayTouch.dylib.
It avoids the upstream packaging script, which previously produced a combined
IPA missing the SideStore executable. Run scripts/package-touch-probe.ps1 with
-Module pointing to the downloaded dylib. It checks the working base IPA hash,
replaces only TweakLoader, and verifies required executables. Sign with iLoader.
Keep the previous IPA for rollback. No signing credentials enter GitHub.
The original automatic build is skipped in this fork to avoid duplicate jobs.

Run node scripts/verify-guest-viewer.cjs to test the actual embedded JavaScript:
text encoding, touch order, coordinates, cancellation, and basic control fallback.
Apple compilation and physical-device tests are separate required checks.

## Sharing

Run a guest in Multitask mode; hold three fingers inside it for 1.2 seconds.
Open the displayed address on the PC and dismiss the phone alert. Repeat the
hold to stop. Each session changes its port and random bearer token. Sharing is
off until requested and bound to Wi-Fi. Anyone with that URL on the LAN can view
and control the guest. HTTP is unencrypted; there is no cloud relay. Requests
and socket waits are bounded.
