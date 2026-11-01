# esphome-mcp-tools

Portable I2C / MCP23017 diagnostics for ESPHome, shared across projects as a git submodule.

- `mcp_tools.h`: `dump_mcp23017()`, `i2c_scan()`, and `McpHealth`, a register health
  monitor (IPOL set, IODIR/IOCON drift, output readback, NACK)
- `mcp_tools.yaml`: package with the `dump_mcp23017_registers(addr)` and `i2c_bus_scan`
  scripts

## Use

```sh
git submodule add https://github.com/Rocco83/esphome-mcp-tools.git mcp_tools
```

```yaml
esphome:
  includes:
    - mcp_tools/mcp_tools.h

packages:
  mcp_tools: !include
    file: mcp_tools/mcp_tools.yaml
    vars:
      i2c_id: bus_i2c
```

## What the health check verifies

`McpHealth::check()` reports an issue if any of these fails. Two kinds:

**Absolute invariants** — true of any healthy MCP23017, whatever the project:

| Check | Why it matters |
|---|---|
| Chip answers (no NACK) | gone from the bus |
| `IOCON` read from `0x0A` == read from `0x0B` | it is **one physical register** mirrored at both addresses. Two different values is impossible on working silicon. This is the check that identified a chip which had looked merely "flaky" for weeks, reporting `0x42` / `0x00`. |
| `IPOL == 0` | ESPHome inverts in software. A stray `0xFF` here silently reports every input as its opposite — a whole floor shown as ON. |
| `DEFVAL == 0` | never written by ESPHome, so a non-zero value means something wrote a register nothing should write: a corrupted transaction. |

**Drift from baseline** — `IODIR`, `GPPU`, `GPINTEN` and `IOCON` are captured on
first sight and compared from then on, so no project-specific expected values
need configuring. `GPINTEN` doubles as a stuck-bit test: a bit that will not
accept a write is a damaged register.

Plus, for pins configured as outputs, that the real pin level matches `OLAT` —
i.e. the drivers are still driving.

**Baseline blind spot:** if a chip is already in a bad state at the first check,
that state becomes the reference and drift never fires. The absolute invariants
have no such weakness, which is why both kinds exist.

**What "clean" does NOT cover:** whether the GPIO register still tracks the
physical pins. A chip can report every register correctly and still have a
frozen GPIO. Use `gpio_moving()` for a hint, or ground a pin and read it back
for proof.

## Repair

`repair()` / `repair_all()` rewrite only the registers that drifted, forcing
`IPOL` to 0 and the rest back to baseline, then read back to confirm. A write
that does not stick is logged as such — that is the signature of real damage,
as opposed to a corrupted transaction, which repairs cleanly.

Writes happen only when something is actually wrong: on a marginal bus every
extra transaction is itself a risk.

Health check from your own interval:

```yaml
interval:
  - interval: 10s
    then:
      - lambda: |-
          std::string d;
          bool dirty = mcp_tools::health().check_all(id(bus_i2c), d, {0x20, 0x24});
```

Buttons stay in the project file (one per chip), see the header of `mcp_tools.yaml`.

## Update in a project

```sh
git submodule update --remote mcp_tools && git add mcp_tools && git commit -m "Bump mcp_tools"
```

Clone a project with `git clone --recurse-submodules`, or run
`git submodule update --init` afterwards.
