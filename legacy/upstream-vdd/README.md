# Upstream VDD source (pre-fork, quarantined)

This directory holds the code this repository was forked from, kept
verbatim for provenance and git history (`git log --follow` on any file
here resolves through to
[VirtualDrivers/Virtual-Display-Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)).
It is **not built by the top-level solution** and its runtime control
model — a `vdd_settings.xml` file, a registry path lookup, and a named
pipe used by PnP-toggle scripts (`Community Scripts/*.ps1`) requiring
device uninstall/reinstall or a `RELOAD_DRIVER` cycle to apply most
changes — has been superseded for Jochona's purposes by the IOCTL-driven
protocol in `driver/JochonaDisplayAdapter` (see
`../../docs/PROTOCOL.md`).

Contents:

- `Virtual Display Driver (HDR)/` — the MttVDD UMDF/IddCx driver
  `driver/JochonaDisplayAdapter` was derived from.
- `Virtual-Audio-Driver (Latest Stable)/` — upstream's companion virtual
  audio driver. Out of scope for the Jochona display-adapter contract;
  kept only for provenance, not adapted or built.
- `Community Scripts/` — the PnP-toggle PowerShell scripts
  (`toggle-VDD.ps1`, `changeres-VDD.ps1`, `virtual-driver-manager.ps1`,
  etc.) that drove the pre-fork XML/named-pipe control surface. None of
  these apply to `driver/JochonaDisplayAdapter`; do not run them against
  it.
- `version.xml` — the upstream "Virtual Driver Control" app's update-feed
  manifest. Unrelated to Jochona's release process.

License: MIT, see `../../NOTICE.md` for the full text and upstream
attribution (this directory is itself the evidence for that provenance
map).
