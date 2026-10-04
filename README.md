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
