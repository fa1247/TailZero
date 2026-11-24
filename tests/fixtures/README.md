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
| `set-exit-node.txt` (+`.stderr`, `.exit`) | exit-node command result; successful selections update subsequent status in memory, without changing fixture files or unrelated preferences |
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
| `exit-nodes` | selected IPv4 exit, IPv6-only candidate, offline candidate and ordinary peer; selection/switch/disable transitions |
| `exit-node-permission-denied` | exit-node selection/disable denied; offline selected node missing from peer list stays selected |
| `bad-json` | malformed `status --json` document |
| `daemon-down` | tailscaled not running (stderr only, no JSON) |

`raw/` holds parser-only samples that are not scenarios (currently the human
readable `tailscale netcheck` output). The identifiers in all fixtures are
sanitized; the JSON shape is exactly what Tailscale 1.104.5 on Debian 13 arm64
emits.
