# TailZero operator setup

TailZero runs as an ordinary Linux user and never asks for a root password.
Read-only Tailscale commands work for any user, but **Connect** and **Disconnect**
(`tailscale up` / `tailscale down`) are privileged operations. Tailscale solves
this with a per-daemon *operator* user, configured once by an administrator.

This is a deployment step. It is not done by the app, by the `.deb` post-install
script, or by any hidden `sudo` call.

## One-time step

Run on the Cardputer Zero, as the user that will launch TailZero (normally the
desktop user; replace `$USER` with that account if needed):

```bash
sudo tailscale set --operator=$USER
```

`$USER` expands to the current login name. If you are configuring it on behalf
of someone else, pass that account explicitly:

```bash
sudo tailscale set --operator=<desktop-user>
```

The operator applies to the local `tailscaled` daemon and survives reboots and
Tailscale upgrades (it is stored in the daemon's preferences).

### Verify

```bash
tailscale debug prefs | grep -i operator     # OperatorUser should be your user
tailscale status                             # read-only status; works for everyone
```

Then reopen TailZero: *Home → Connect / Disconnect* should work instead of
showing the permission hint.

### Revert

To remove the operator grant (for example when retiring the device):

```bash
sudo tailscale set --operator=""
```

Check `tailscale set --help` on the installed version for the exact syntax; an
empty operator restores the default "root only" behavior.

## What happens without it

The read-only pages (status, device list, ping, netcheck) keep working. When
Connect/Disconnect fails with a permission error, TailZero shows:

```text
No permission. Ask the administrator to run:
sudo tailscale set --operator=$USER
```

and does not retry, escalate privileges, or crash. TailZero never reads or stores
credentials.

## Why the app must not run as root

- The Cardputer Zero Application Store forbids applications that run as root.
- A root GUI process would give the whole LVGL/input/render stack privileged
  access for no reason; TailZero only needs to exec a CLI with the caller's UID.
- The app never edits `sudoers`, never calls `sudo`, and never accepts a password
  in its UI (see ROADMAP §4 and the store policy).
- The shipped package installs no systemd service and nothing that starts at
  boot; the launcher runs `tailzero` as the logged-in user.

## What the operator grant does *not* allow

`tailscale set --operator` allows `up`/`down` and a few preference commands for
that user. It does not let the app install packages, modify `tailscaled`
configuration files, or stop the `tailscaled` service, and TailZero does not try
to do any of those things.
