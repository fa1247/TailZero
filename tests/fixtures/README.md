# Test fixtures

Every directory here is a **mock scenario** for `TAILZERO_MOCK_DIR`:

```sh
# on the device / any Linux box, from the repository root
TAILZERO_MOCK_DIR=tests/fixtures/connected ./tailzero
```

`src/tailscale/mock_backend.h` documents the file naming convention. Quick
summary:

| file | meaning |
| --- | --- |
| `status.json` / `status.txt` | stdout of `tailscale status --json` |
| `status.stderr`, `status.exit` | stderr / exit code override for that command |
| `netcheck.json` / `netcheck.txt` | netcheck output (JSON is tried first) |
| `derp-map.json` | optional `tailscale debug derp-map` output, used to turn the numeric `PreferredDERP` id of the JSON netcheck report into a region code |
| `up.txt` / `down.txt` (+`.exit`) | connect / disconnect output |
| `version.txt`, `prefs.json`, `ip4.txt`, `ip6.txt` | misc commands (`operator.txt` is an accepted alias for `prefs.json`) |
| `ping-<ip>.txt` (+`.exit`) | `tailscale ping <ip>` output (`ping.txt` is the generic fallback) |

Scenarios:

| scenario | what it exercises |
| --- | --- |
| `connected` | Running, direct + DERP peers, successful ping fixtures, netcheck |
| `disconnected` | `tailscale down` (BackendState `Stopped`) |
| `needs-login` | `NeedsLogin` + `AuthURL` |
| `empty-peers` | running tailnet with no peers |
| `multi-peers` | online/offline mix, direct, relay, unknown path, exit node, missing hostname, IPv6-only peer |
| `offline-peer` | peer offline + ping timeout |
| `permission-denied` | `tailscale up`/`down` without operator permission |
| `bad-json` | malformed `status --json` document |
| `daemon-down` | tailscaled not running (stderr only, no JSON) |

`raw/` holds parser-only samples that are not scenarios (currently the human
readable `tailscale netcheck` output). The identifiers in all fixtures are
sanitized; the JSON shape is exactly what Tailscale 1.104.5 on Debian 13 arm64
emits.
