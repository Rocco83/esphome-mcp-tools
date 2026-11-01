#pragma once
//
// Portable I2C / MCP23017 diagnostics for ESPHome.
// Add this repo as a git submodule next to your YAML, then:
//
//   esphome:
//     includes:
//       - mcp_tools/mcp_tools.h
//
// Then call from any lambda:
//
//   mcp_tools::dump_mcp23017(id(bus_i2c), 0x20);
//   mcp_tools::i2c_scan(id(bus_i2c));
//
// Nothing here is project specific: one repo, shared as a submodule.
//
#include "esphome/core/log.h"
#include "esphome/components/i2c/i2c.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace mcp_tools {

static const char *const TAG_DUMP = "mcp_dump";
static const char *const TAG_SCAN = "i2c_scan";

// Render a byte MSB..LSB into buf[9].
inline void byte_to_bin(uint8_t v, char *buf) {
  for (int b = 0; b < 8; b++)
    buf[7 - b] = ((v >> b) & 1) ? '1' : '0';
  buf[8] = '\0';
}

// Full MCP23017 register dump, A/B side by side.
// Assumes IOCON.BANK = 0 (power-on default, and what ESPHome uses).
inline void dump_mcp23017(esphome::i2c::I2CBus *bus, uint8_t addr) {
  struct Reg {
    const char *name;
    uint8_t a;
    uint8_t b;
  };
  static const Reg REGS[] = {
      {"IODIR", 0x00, 0x01},    // 1 = input, 0 = output
      {"IPOL", 0x02, 0x03},     // 1 = value inverted in GPIO reg
      {"GPINTEN", 0x04, 0x05},  // interrupt-on-change enable
      {"DEFVAL", 0x06, 0x07},
      {"INTCON", 0x08, 0x09},
      {"IOCON", 0x0A, 0x0B},  // BANK, MIRROR, SEQOP, ODR, INTPOL
      {"GPPU", 0x0C, 0x0D},   // 1 = 100k pull-up on
      {"INTF", 0x0E, 0x0F},
      {"INTCAP", 0x10, 0x11},
      {"GPIO", 0x12, 0x13},  // raw level seen at the pin
      {"OLAT", 0x14, 0x15},
  };
  const uint8_t n = sizeof(REGS) / sizeof(REGS[0]);

  esphome::i2c::I2CDevice dev;
  dev.set_i2c_bus(bus);
  dev.set_i2c_address(addr);
  auto rd = [&dev](uint8_t reg, uint8_t &out) -> bool {
    return dev.read_register(reg, &out, 1) == esphome::i2c::ERROR_OK;
  };

  ESP_LOGI(TAG_DUMP, "########## MCP23017 @ 0x%02X ##########", addr);

  uint8_t gpio_a = 0, gpio_b = 0, ipol_a = 0, ipol_b = 0;
  uint8_t iodir_a = 0, iodir_b = 0, gppu_a = 0, gppu_b = 0;
  bool any_fail = false;

  for (uint8_t r = 0; r < n; r++) {
    uint8_t va = 0, vb = 0;
    const bool oka = rd(REGS[r].a, va);
    const bool okb = rd(REGS[r].b, vb);
    if (!oka || !okb) {
      ESP_LOGW(TAG_DUMP, "  %-8s READ FAILED (A:%d B:%d)", REGS[r].name, oka, okb);
      any_fail = true;
      continue;
    }
    char ba[9], bb[9];
    byte_to_bin(va, ba);
    byte_to_bin(vb, bb);
    ESP_LOGI(TAG_DUMP, "  %-8s A=0x%02X (%s)  B=0x%02X (%s)", REGS[r].name, va, ba, vb, bb);
    switch (REGS[r].a) {
      case 0x00: iodir_a = va; iodir_b = vb; break;
      case 0x02: ipol_a = va;  ipol_b = vb;  break;
      case 0x0C: gppu_a = va;  gppu_b = vb;  break;
      case 0x12: gpio_a = va;  gpio_b = vb;  break;
      default: break;
    }
  }

  if (any_fail) {
    ESP_LOGW(TAG_DUMP, "  one or more registers did not answer — check wiring/address");
  }

  ESP_LOGI(TAG_DUMP, "  --- per pin: raw GPIO / IPOL / direction / pull-up ---");
  for (uint8_t pin = 0; pin < 16; pin++) {
    const bool hi = pin >= 8;
    const uint8_t sh = pin % 8;
    const uint8_t g = ((hi ? gpio_b : gpio_a) >> sh) & 1;
    const uint8_t i = ((hi ? ipol_b : ipol_a) >> sh) & 1;
    const uint8_t d = ((hi ? iodir_b : iodir_a) >> sh) & 1;
    const uint8_t p = ((hi ? gppu_b : gppu_a) >> sh) & 1;
    ESP_LOGI(TAG_DUMP, "  %s%u (pin %2u): GPIOraw=%u  IPOL=%u  DIR=%-3s  PU=%u",
             hi ? "B" : "A", sh, pin, g, i, d ? "in" : "out", p);
  }
  ESP_LOGI(TAG_DUMP, "########## END 0x%02X ##########", addr);
}

// Probe every 7-bit address with a zero-length write: an ACK means something
// is there. ESPHome's own boot scan uses writev(), but that is not public on
// I2CBus, so we go through write() -- same bus transaction (address + stop).
// Returns the number of devices found.
inline int i2c_scan(esphome::i2c::I2CBus *bus) {
  ESP_LOGI(TAG_SCAN, "--- I2C scan ---");
  int found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    if (bus->write(addr, nullptr, 0) != esphome::i2c::ERROR_OK)
      continue;
    const char *hint = "";
    switch (addr) {
      case 0x20 ... 0x27: hint = "  (MCP23017 / PCF8574 range)"; break;
      case 0x50 ... 0x57: hint = "  (EEPROM range)"; break;
      case 0x68: hint = "  (DS1307/DS3231 RTC, MPU6050)"; break;
      case 0x6F: hint = "  (MCP7940M RTC)"; break;
      case 0x3C: case 0x3D: hint = "  (SSD1306 OLED)"; break;
      case 0x76: case 0x77: hint = "  (BMP/BME280)"; break;
      default: break;
    }
    ESP_LOGI(TAG_SCAN, "  found 0x%02X%s", addr, hint);
    found++;
  }
  ESP_LOGI(TAG_SCAN, "--- scan done: %d device(s) ---", found);
  return found;
}

// --- Register health monitor ---
// Flags per chip:
//   IPOL != 0            ESPHome never sets it (outputs included)
//   IODIR/IOCON drift    vs first read; a chip reset sets IODIR=0xFF
//   output readback      pin level != OLAT on output pins
//   NACK                 chip not answering
//
//   std::string d;
//   bool dirty = mcp_tools::health().check_all(id(bus_i2c), d, {0x20, 0x24});

static const char *const TAG_HEALTH = "mcp_health";

// Register health monitor.
//
// Two kinds of check:
//
//   ABSOLUTE INVARIANTS - true of any healthy MCP23017, whatever the project:
//     * IOCON is ONE physical register mirrored at 0x0A and 0x0B. Reading two
//       different values from it is impossible on working silicon. This is the
//       check that identified a chip which had looked merely "flaky" for weeks.
//     * IPOL must be 0: ESPHome inverts in software, and a stray 0xFF here
//       silently reports every input as its opposite.
//     * DEFVAL/OLAT left non-zero on an input-only port means something wrote
//       a register nothing should be writing - a corrupted transaction.
//
//   DRIFT FROM BASELINE - IODIR, GPPU, GPINTEN and IOCON are captured on first
//     sight and compared from then on, so no project-specific expected values
//     need to be configured.
//
// Note the baseline's weakness: if a chip is already in a bad state at the
// first check, that state becomes the reference. The absolute invariants above
// have no such blind spot, which is why both kinds exist.
class McpHealth {
 public:
  // One chip. Appends "0xNN:<issues> " to detail; true if anomalous.
  bool check(esphome::i2c::I2CBus *bus, uint8_t addr, std::string &detail) {
    Chip &c = chips_[addr];
    Regs r;
    std::string issues;
    char buf[80];
    if (!read_(bus, addr, r)) {
      issues = "NACK ";
    } else {
      if (!c.seen) {
        c.seen = true;
        c.base = r;
        ESP_LOGI(TAG_HEALTH, "0x%02X baseline IODIR A=%02X B=%02X IOCON=%02X GPPU A=%02X B=%02X",
                 addr, r.iodir_a, r.iodir_b, r.iocon, r.gppu_a, r.gppu_b);
      }

      // --- absolute invariants ---
      if (r.iocon != r.iocon_b) {
        snprintf(buf, sizeof(buf), "IOCON MIRROR %02X!=%02X ", r.iocon, r.iocon_b);
        issues += buf;  // same physical register: a split means damaged silicon
      }
      if (r.ipol_a || r.ipol_b) {
        snprintf(buf, sizeof(buf), "IPOL A=%02X,B=%02X ", r.ipol_a, r.ipol_b);
        issues += buf;
      }
      if (r.defval_a || r.defval_b) {
        snprintf(buf, sizeof(buf), "DEFVAL A=%02X,B=%02X ", r.defval_a, r.defval_b);
        issues += buf;  // never written by ESPHome: indicates a stray write
      }

      // --- drift from baseline ---
      if (r.iodir_a != c.base.iodir_a || r.iodir_b != c.base.iodir_b) {
        snprintf(buf, sizeof(buf), "IODIR A=%02X>%02X,B=%02X>%02X ",
                 c.base.iodir_a, r.iodir_a, c.base.iodir_b, r.iodir_b);
        issues += buf;
      }
      if (r.iocon != c.base.iocon) {
        snprintf(buf, sizeof(buf), "IOCON %02X>%02X ", c.base.iocon, r.iocon);
        issues += buf;
      }
      if (r.gppu_a != c.base.gppu_a || r.gppu_b != c.base.gppu_b) {
        snprintf(buf, sizeof(buf), "GPPU A=%02X>%02X,B=%02X>%02X ",
                 c.base.gppu_a, r.gppu_a, c.base.gppu_b, r.gppu_b);
        issues += buf;
      }
      if (r.gpinten_a != c.base.gpinten_a || r.gpinten_b != c.base.gpinten_b) {
        snprintf(buf, sizeof(buf), "GPINTEN A=%02X>%02X,B=%02X>%02X ",
                 c.base.gpinten_a, r.gpinten_a, c.base.gpinten_b, r.gpinten_b);
        issues += buf;  // a bit that will not take a write is a damaged register
      }

      // output pins (IODIR=0): real pin level must match the latch
      const uint8_t bad_a = (uint8_t) (~r.iodir_a & (r.gpio_a ^ r.ipol_a ^ r.olat_a));
      const uint8_t bad_b = (uint8_t) (~r.iodir_b & (r.gpio_b ^ r.ipol_b ^ r.olat_b));
      if (bad_a || bad_b) {
        snprintf(buf, sizeof(buf), "OUT!=OLAT A=%02X,B=%02X ", bad_a, bad_b);
        issues += buf;
      }
    }
    if (issues != c.last) {  // log transitions only
      if (issues.empty())
        ESP_LOGI(TAG_HEALTH, "0x%02X clean again", addr);
      else
        ESP_LOGW(TAG_HEALTH, "0x%02X %s", addr, issues.c_str());
      c.last = issues;
    }
    if (issues.empty())
      return false;
    snprintf(buf, sizeof(buf), "0x%02X:", addr);
    detail += buf;
    detail += issues;
    return true;
  }

  // Every chip in addrs.
  bool check_all(esphome::i2c::I2CBus *bus, std::string &detail,
                 const std::vector<uint8_t> &addrs = {0x20}) {
    bool dirty = false;
    for (uint8_t addr : addrs)
      dirty |= check(bus, addr, detail);
    return dirty;
  }

  // Is the GPIO register still sampling, or stuck? Reads twice and reports
  // whether anything moved. Informational: genuinely static inputs also read
  // stable, so this is a hint, not a verdict.
  bool gpio_moving(esphome::i2c::I2CBus *bus, uint8_t addr, uint32_t gap_ms = 2) {
    uint8_t a1 = 0, b1 = 0, a2 = 0, b2 = 0;
    esphome::i2c::I2CDevice dev;
    dev.set_i2c_bus(bus);
    dev.set_i2c_address(addr);
    auto rd = [&dev](uint8_t reg, uint8_t &out) {
      return dev.read_register(reg, &out, 1) == esphome::i2c::ERROR_OK;
    };
    if (!rd(0x12, a1) || !rd(0x13, b1)) return false;
    delay(gap_ms);
    if (!rd(0x12, a2) || !rd(0x13, b2)) return false;
    ESP_LOGD(TAG_HEALTH, "0x%02X GPIO A=%02X/%02X B=%02X/%02X %s",
             addr, a1, a2, b1, b2, (a1 == a2 && b1 == b2) ? "(stable)" : "(moving)");
    return a1 != a2 || b1 != b2;
  }

  // Verify-and-repair. Rewrites only what drifted, then reads back to confirm.
  // IPOL is forced to 0 (absolute invariant); the rest returns to the baseline.
  // Returns how many registers had to be rewritten; a write that does not stick
  // is logged as such, because that is the signature of real damage rather than
  // a corrupted transaction.
  //
  // Writes only when something is wrong: on a marginal bus every extra
  // transaction is itself a risk.
  int repair(esphome::i2c::I2CBus *bus, uint8_t addr) {
    Chip &c = chips_[addr];
    if (!c.seen) return 0;  // no baseline yet, nothing to compare against
    Regs r;
    if (!read_(bus, addr, r)) return 0;

    esphome::i2c::I2CDevice dev;
    dev.set_i2c_bus(bus);
    dev.set_i2c_address(addr);
    auto rd = [&dev](uint8_t reg, uint8_t &out) {
      return dev.read_register(reg, &out, 1) == esphome::i2c::ERROR_OK;
    };
    auto wr = [&dev](uint8_t reg, uint8_t v) {
      return dev.write_register(reg, &v, 1) == esphome::i2c::ERROR_OK;
    };

    struct Fix { uint8_t reg; const char *name; uint8_t is, want; };
    const Fix fixes[] = {
        {0x02, "IPOLA", r.ipol_a, 0x00},
        {0x03, "IPOLB", r.ipol_b, 0x00},
        {0x00, "IODIRA", r.iodir_a, c.base.iodir_a},
        {0x01, "IODIRB", r.iodir_b, c.base.iodir_b},
        {0x0C, "GPPUA", r.gppu_a, c.base.gppu_a},
        {0x0D, "GPPUB", r.gppu_b, c.base.gppu_b},
        {0x0A, "IOCON", r.iocon, c.base.iocon},
    };
    int fixed = 0;
    for (const auto &f : fixes) {
      if (f.is == f.want) continue;
      uint8_t back = 0;
      const bool ok = wr(f.reg, f.want) && rd(f.reg, back) && back == f.want;
      ESP_LOGW(TAG_HEALTH, "0x%02X %s was %02X, expected %02X -> %s", addr, f.name,
               f.is, f.want, ok ? "REPAIRED" : "REPAIR FAILED (suspect hardware)");
      fixed++;
    }
    return fixed;
  }

  int repair_all(esphome::i2c::I2CBus *bus, const std::vector<uint8_t> &addrs = {0x20}) {
    int n = 0;
    for (uint8_t addr : addrs)
      n += repair(bus, addr);
    return n;
  }

 private:
  struct Regs {
    uint8_t iodir_a, iodir_b, ipol_a, ipol_b, iocon, iocon_b;
    uint8_t gpinten_a, gpinten_b, defval_a, defval_b, gppu_a, gppu_b;
    uint8_t gpio_a, gpio_b, olat_a, olat_b;
  };
  struct Chip {
    bool seen = false;
    Regs base{};
    std::string last;
  };
  std::map<uint8_t, Chip> chips_;

  static bool read_(esphome::i2c::I2CBus *bus, uint8_t addr, Regs &r) {
    esphome::i2c::I2CDevice dev;
    dev.set_i2c_bus(bus);
    dev.set_i2c_address(addr);
    auto rd = [&dev](uint8_t reg, uint8_t &out) {
      return dev.read_register(reg, &out, 1) == esphome::i2c::ERROR_OK;
    };
    return rd(0x00, r.iodir_a) && rd(0x01, r.iodir_b) && rd(0x02, r.ipol_a) &&
           rd(0x03, r.ipol_b) && rd(0x04, r.gpinten_a) && rd(0x05, r.gpinten_b) &&
           rd(0x06, r.defval_a) && rd(0x07, r.defval_b) && rd(0x0A, r.iocon) &&
           rd(0x0B, r.iocon_b) && rd(0x0C, r.gppu_a) && rd(0x0D, r.gppu_b) &&
           rd(0x12, r.gpio_a) && rd(0x13, r.gpio_b) && rd(0x14, r.olat_a) &&
           rd(0x15, r.olat_b);
  }
};

// Shared instance, so baselines survive across lambdas.
inline McpHealth &health() {
  static McpHealth h;
  return h;
}

}  // namespace mcp_tools
