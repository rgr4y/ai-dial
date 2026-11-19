#pragma once

#include <utility>

#include "esphome/components/spi/spi.h"
#include "esphome/components/display/display.h"
#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
// TE patch
#ifdef USE_ESP32
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace esphome::mipi_spi {

constexpr static const char *const TAG = "display.mipi_spi";

// Maximum bytes to log for commands (truncated if larger)
static constexpr size_t MIPI_SPI_MAX_CMD_LOG_BYTES = 64;

// Command codes for MIPI SPI displays. Not all currently used, kept here for reference.
static constexpr uint8_t SW_RESET_CMD = 0x01;
static constexpr uint8_t SLEEP_OUT = 0x11;
static constexpr uint8_t NORON = 0x13;
static constexpr uint8_t INVERT_OFF = 0x20;
static constexpr uint8_t INVERT_ON = 0x21;
static constexpr uint8_t ALL_ON = 0x23;
static constexpr uint8_t WRAM = 0x24;
static constexpr uint8_t MIPI = 0x26;
static constexpr uint8_t DISPLAY_ON = 0x29;
static constexpr uint8_t RASET = 0x2B;
static constexpr uint8_t CASET = 0x2A;
static constexpr uint8_t WDATA = 0x2C;
static constexpr uint8_t TEON = 0x35;
static constexpr uint8_t MADCTL_CMD = 0x36;
static constexpr uint8_t PIXFMT = 0x3A;
static constexpr uint8_t BRIGHTNESS = 0x51;
static constexpr uint8_t SWIRE1 = 0x5A;
static constexpr uint8_t SWIRE2 = 0x5B;
static constexpr uint8_t PAGESEL = 0xFE;

static constexpr uint8_t MADCTL_MY = 0x80;           // Bit 7 Bottom to top
static constexpr uint8_t MADCTL_MX = 0x40;           // Bit 6 Right to left
static constexpr uint8_t MADCTL_MV = 0x20;           // Bit 5 Swap axes
static constexpr uint8_t MADCTL_RGB = 0x00;          // Bit 3 Red-Green-Blue pixel order
static constexpr uint8_t MADCTL_BGR = 0x08;          // Bit 3 Blue-Green-Red pixel order
static constexpr uint8_t MADCTL_XFLIP = 0x02;        // Mirror the display horizontally
static constexpr uint8_t MADCTL_YFLIP = 0x01;        // Mirror the display vertically
static constexpr uint16_t MADCTL_FLIP_FLAG = 0x100;  // controller uses axis flip bits

static constexpr uint8_t DELAY_FLAG = 0xFF;
// store a 16 bit value in a buffer, big endian.
static inline void put16_be(uint8_t *buf, uint16_t value) {
  buf[0] = value >> 8;
  buf[1] = value;
}

// Buffer mode, conveniently also the number of bytes in a pixel
enum PixelMode {
  PIXEL_MODE_8 = 1,
  PIXEL_MODE_16 = 2,
  PIXEL_MODE_18 = 3,
};

enum BusType {
  BUS_TYPE_SINGLE = 1,
  BUS_TYPE_QUAD = 4,
  BUS_TYPE_OCTAL = 8,
  BUS_TYPE_SINGLE_16 = 16,  // Single bit bus, but 16 bits per transfer
};

// Helper function for dump_config - defined in mipi_spi.cpp to allow use of LOG_PIN macro
void internal_dump_config(const char *model, int width, int height, int offset_width, int offset_height, uint8_t madctl,
                          bool invert_colors, int display_bits, bool is_big_endian, const optional<uint8_t> &brightness,
                          GPIOPin *cs, GPIOPin *reset, GPIOPin *dc, int spi_mode, uint32_t data_rate, int bus_width,
                          bool has_hardware_rotation);

// Lets a light set the display brightness without knowing the display's template parameters.
class MipiSpiBrightness {
 public:
  virtual void set_brightness(uint8_t brightness) = 0;
};

// TE patch: tearing-effect sync state. Not a template, so the ISR can live in mipi_spi.cpp.
#ifdef USE_ESP32
struct TeSync {
  static void gpio_intr(TeSync *arg);
  SemaphoreHandle_t sem{nullptr};
  volatile int64_t last_us{0};  // time of the latest TE edge
  volatile uint32_t edges{0};
  int64_t start_te_us{0};  // last_us when the current write started
  // Stats, logged and reset every 5s
  int64_t period_us{0};  // TE period measured over the previous window
  int64_t stats_start_us{0};
  uint32_t stats_edges{0};
  uint32_t chunks{0}, waits{0}, timeouts{0}, crossings{0};
  int64_t wait_us{0}, write_us{0}, write_px{0};
  float us_per_px{0};  // write speed measured over the previous window
};
#endif

/**
 * Base class for MIPI SPI displays.
 * All the methods are defined here in the header file, as it is not possible to define templated methods in a cpp file.
 *
 * @tparam BUFFERTYPE The type of the buffer pixels, e.g. uint8_t or uint16_t
 * @tparam BUFFERPIXEL Color depth of the buffer
 * @tparam DISPLAYPIXEL Color depth of the display
 * @tparam BUS_TYPE The type of the interface bus (single, quad, octal)
 * @tparam WIDTH Width of the display in pixels
 * @tparam HEIGHT Height of the display in pixels
 * @tparam OFFSET_WIDTH The x-offset of the display in pixels
 * @tparam OFFSET_HEIGHT The y-offset of the display in pixels
 * @tparam PAD_WIDTH Additional pixels recognised by the controller after the offset and width
 * @tparam PAD_HEIGHT Additional lines recognised by the controller after the offset and width
 * @tparam MADCTL The base MADCTL value for the display, with no rotation bits set.
 * @tparam HAS_HARDWARE_ROTATION Whether the display supports hardware rotation.
 * buffer
 */
template<typename BUFFERTYPE, PixelMode BUFFERPIXEL, bool IS_BIG_ENDIAN, PixelMode DISPLAYPIXEL, BusType BUS_TYPE,
         int WIDTH, int HEIGHT, int OFFSET_WIDTH, int OFFSET_HEIGHT, int PAD_WIDTH, int PAD_HEIGHT, uint16_t MADCTL,
         bool HAS_HARDWARE_ROTATION>
class MipiSpi : public display::Display,
                public MipiSpiBrightness,
                public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                      spi::DATA_RATE_1MHZ> {
 public:
  MipiSpi() = default;
  void update() override { this->stop_poller(); }
  void draw_pixel_at(int x, int y, Color color) override {}
  void set_model(const char *model) { this->model_ = model; }
  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_enable_pins(std::vector<GPIOPin *> enable_pins) { this->enable_pins_ = std::move(enable_pins); }
  void set_dc_pin(GPIOPin *dc_pin) { this->dc_pin_ = dc_pin; }
  // TE patch
  void set_te_pin(InternalGPIOPin *te_pin) { this->te_pin_ = te_pin; }
  void set_te_timeout(uint32_t ms) { this->te_timeout_ms_ = ms; }
  void set_invert_colors(bool invert_colors) {
    this->invert_colors_ = invert_colors;
    this->reset_params_();
  }
  void set_brightness(uint8_t brightness) override {
    this->brightness_ = brightness;
    // Before setup the stored value is applied by reset_params_()
    if (this->is_ready())
      this->write_command_(BRIGHTNESS, brightness);
  }
  void set_rotation(display::DisplayRotation rotation) override {
    this->rotation_ = rotation;
    if constexpr (HAS_HARDWARE_ROTATION) {
      this->reset_params_();
    }
  }
  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }

  int get_width() override {
    if (this->rotation_ == display::DISPLAY_ROTATION_90_DEGREES ||
        this->rotation_ == display::DISPLAY_ROTATION_270_DEGREES)
      return HEIGHT;
    return WIDTH;
  }

  int get_height() override {
    if (this->rotation_ == display::DISPLAY_ROTATION_90_DEGREES ||
        this->rotation_ == display::DISPLAY_ROTATION_270_DEGREES)
      return WIDTH;
    return HEIGHT;
  }

  void set_init_sequence(const uint8_t *sequence, size_t len) {
    this->init_sequence_ = sequence;
    this->init_sequence_len_ = len;
  }

  // reset the display, and write the init sequence
  void setup() override {
    this->spi_setup();
    if (this->dc_pin_ != nullptr) {
      this->dc_pin_->setup();
      this->dc_pin_->digital_write(false);
    }
    for (auto *pin : this->enable_pins_) {
      pin->setup();
      pin->digital_write(true);
    }
    if (this->reset_pin_ != nullptr) {
      this->reset_pin_->setup();
      this->reset_pin_->digital_write(true);
      delay(5);
      this->reset_pin_->digital_write(false);
      delay(5);
      this->reset_pin_->digital_write(true);
      // required delay after reset is already in the init sequence, don't duplicate
    }

    // need to know when the display is ready for SLPOUT command - will be 120ms after reset
    auto when = millis() + 120;
    size_t index = 0;
    const uint8_t *seq = this->init_sequence_;
    const size_t len = this->init_sequence_len_;
    while (index != len) {
      if (len - index < 2) {
        esph_log_e(TAG, "Malformed init sequence");
        this->mark_failed();
        return;
      }
      uint8_t cmd = progmem_read_byte(seq + index++);
      uint8_t x = progmem_read_byte(seq + index++);
      if (x == DELAY_FLAG) {
        if (cmd == 0) {
          cmd = clamp_at_least((int) (when - millis()), 0);
        }
        esph_log_d(TAG, "Delay %dms", cmd);
        delay(cmd);
      } else {
        uint8_t num_args = x & 0x7F;
        if (len - index < num_args) {
          esph_log_e(TAG, "Malformed init sequence");
          this->mark_failed();
          return;
        }
        // The sequence is in flash, which SPI DMA (and ESP8266 byte loads) cannot read
        uint8_t args[0x80];
        progmem_memcpy(args, seq + index, num_args);
        this->write_command_(cmd, args, num_args);
        index += num_args;
      }
    }
    this->reset_params_();
    this->te_setup_();  // TE patch
    // Marks init as done, so later commands log at verbose level instead of debug
    this->init_sequence_len_ = 0;
  }

  // Drawing operations

  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) override {
    if (this->is_failed())
      return;
    if (w <= 0 || h <= 0)
      return;
    if (get_pixel_mode(bitness) != BUFFERPIXEL || big_endian != IS_BIG_ENDIAN) {
      // note that the usual logging macros are banned in header files, so use their replacement
      esph_log_e(TAG, "Unsupported color depth or bit order");
      return;
    }
    this->write_to_display_(x_start, y_start, w, h, reinterpret_cast<const BUFFERTYPE *>(ptr), x_offset, y_offset,
                            x_pad);
  }

  void dump_config() override {
    internal_dump_config(this->model_, this->get_width(), this->get_height(), this->get_offset_width_(),
                         this->get_offset_height_(), (uint8_t) MADCTL, this->invert_colors_, DISPLAYPIXEL * 8,
                         IS_BIG_ENDIAN, this->brightness_, this->cs_, this->reset_pin_, this->dc_pin_, this->mode_,
                         this->data_rate_, BUS_TYPE, HAS_HARDWARE_ROTATION);
  }

 protected:
  /* METHODS */
  // If hardware rotation is in use, the actual display width/height changes with rotation
  int get_width_internal() override {
    if constexpr (HAS_HARDWARE_ROTATION)
      return get_width();
    return WIDTH;
  }
  int get_height_internal() override {
    if constexpr (HAS_HARDWARE_ROTATION)
      return get_height();
    return HEIGHT;
  }
  // convenience functions to write commands with or without data
  void write_command_(uint8_t cmd, uint8_t data) { this->write_command_(cmd, &data, 1); }
  void write_command_(uint8_t cmd) { this->write_command_(cmd, &cmd, 0); }

  // Writes a command to the display, with the given bytes.
  void write_command_(uint8_t cmd, const uint8_t *bytes, size_t len) {
    char hex_buf[format_hex_pretty_size(MIPI_SPI_MAX_CMD_LOG_BYTES)];
    // Don't spam the log after setup
    if (this->init_sequence_len_ == 0) {
      esph_log_v(TAG, "Command %02X, length %d, bytes %s", cmd, len, format_hex_pretty_to(hex_buf, bytes, len));
    } else {
      esph_log_d(TAG, "Command %02X, length %d, bytes %s", cmd, len, format_hex_pretty_to(hex_buf, bytes, len));
    }
    if constexpr (BUS_TYPE == BUS_TYPE_QUAD) {
      this->enable();
      this->write_cmd_addr_data(8, 0x02, 24, cmd << 8, bytes, len);
      this->disable();
    } else if constexpr (BUS_TYPE == BUS_TYPE_OCTAL) {
      this->enable();
      this->dc_pin_->digital_write(false);
      this->write_cmd_addr_data(0, 0, 0, 0, &cmd, 1, 8);
      this->dc_pin_->digital_write(true);
      // hold the bus between command and data to avoid a glitch on the D/C line
      if (len != 0) {
        this->write_cmd_addr_data(0, 0, 0, 0, bytes, len, 8);
      }
      this->disable();
    } else if constexpr (BUS_TYPE == BUS_TYPE_SINGLE) {
      // Toggle D/C only while holding the bus; works around a quirk in the CoreS3 and W5500 ethernet combination.
      // See https://github.com/esphome/esphome/pull/18529
      this->enable();
      this->dc_pin_->digital_write(false);
      this->write_byte(cmd);
      this->dc_pin_->digital_write(true);
      this->disable();
      if (len != 0) {
        this->enable();
        this->write_array(bytes, len);
        this->disable();
      }
    } else if constexpr (BUS_TYPE == BUS_TYPE_SINGLE_16) {
      // DC must be stable before CS as the clock is gated by CS
      this->dc_pin_->digital_write(false);
      this->enable();
      this->write_byte(cmd);
      this->disable();
      this->dc_pin_->digital_write(true);
      for (size_t i = 0; i != len; i++) {
        // must enable and disable for each byte based on empirical testing
        this->enable();
        this->write_byte(0);
        this->write_byte(bytes[i]);
        this->disable();
      }
    }
  }

  // write changed parameters to the display
  void reset_params_() {
    if (!this->is_ready())
      return;
    this->write_command_(this->invert_colors_ ? INVERT_ON : INVERT_OFF);
    if (this->brightness_.has_value())
      this->write_command_(BRIGHTNESS, this->brightness_.value());

    // calculate new madctl value from base value adjusted for rotation
    uint8_t madctl = (uint8_t) MADCTL;  // lower 8 bits only
    constexpr bool use_flips = (MADCTL & MADCTL_FLIP_FLAG) != 0;
    constexpr uint8_t x_mask = use_flips ? MADCTL_XFLIP : MADCTL_MX;
    constexpr uint8_t y_mask = use_flips ? MADCTL_YFLIP : MADCTL_MY;
    if constexpr (HAS_HARDWARE_ROTATION) {
      switch (this->rotation_) {
        default:
          break;
        case display::DISPLAY_ROTATION_90_DEGREES:
          madctl ^= x_mask;     // flip X axis
          madctl ^= MADCTL_MV;  // swap X and Y axes
          break;
        case display::DISPLAY_ROTATION_180_DEGREES:
          madctl ^= x_mask;  // flip X axis
          madctl ^= y_mask;  // flip Y axis
          break;
        case display::DISPLAY_ROTATION_270_DEGREES:
          madctl ^= y_mask;     // flip Y axis
          madctl ^= MADCTL_MV;  // swap X and Y axes
          break;
      }
    }
    esph_log_d(TAG, "Setting MADCTL for rotation %d, value %X", this->rotation_, madctl);
    this->write_command_(MADCTL_CMD, madctl);
  }

  uint16_t get_offset_width_() const {
    if constexpr (HAS_HARDWARE_ROTATION) {
      switch (this->rotation_) {
        case display::DISPLAY_ROTATION_90_DEGREES:
          return OFFSET_HEIGHT;
        case display::DISPLAY_ROTATION_180_DEGREES:
          return PAD_WIDTH;
        case display::DISPLAY_ROTATION_270_DEGREES:
          return PAD_HEIGHT;
        default:
          break;
      }
    }
    return OFFSET_WIDTH;
  }

  uint16_t get_offset_height_() const {
    if constexpr (HAS_HARDWARE_ROTATION) {
      switch (this->rotation_) {
        case display::DISPLAY_ROTATION_90_DEGREES:
          return PAD_WIDTH;
        case display::DISPLAY_ROTATION_180_DEGREES:
          return PAD_HEIGHT;
        case display::DISPLAY_ROTATION_270_DEGREES:
          return OFFSET_WIDTH;
        default:
          break;
      }
    }
    return OFFSET_HEIGHT;
  }

  // set the address window for the next data write
  void set_addr_window_(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    esph_log_v(TAG, "Set addr %d/%d, %d/%d", x1, y1, x2, y2);
    uint8_t buf[4];
    x1 += get_offset_width_();
    x2 += get_offset_width_();
    y1 += get_offset_height_();
    y2 += get_offset_height_();
    put16_be(buf, y1);
    put16_be(buf + 2, y2);
    this->write_command_(RASET, buf, sizeof buf);
    put16_be(buf, x1);
    put16_be(buf + 2, x2);
    this->write_command_(CASET, buf, sizeof buf);
    if constexpr (BUS_TYPE != BUS_TYPE_QUAD) {
      this->write_command_(WDATA);
    }
  }

  // map the display color bitness to the pixel mode
  static PixelMode get_pixel_mode(display::ColorBitness bitness) {
    switch (bitness) {
      case display::COLOR_BITNESS_888:
        return PIXEL_MODE_18;  // 18 bits per pixel
      case display::COLOR_BITNESS_565:
        return PIXEL_MODE_16;  // 16 bits per pixel
      default:
        return PIXEL_MODE_8;  // Default to 8 bits per pixel
    }
  }

  /**
   * Writes a buffer to the display.
   * @param ptr The pointer to the pixel data
   * @param w Width of each line in bytes
   * @param h Height of the buffer in rows
   * @param stride Total length of each line in bytes, including any padding
   */
  void write_display_data_(const uint8_t *ptr, size_t w, size_t h, size_t stride) {
    if (stride == w) {
      if constexpr (BUS_TYPE == BUS_TYPE_SINGLE || BUS_TYPE == BUS_TYPE_SINGLE_16) {
        this->write_array(ptr, w * h);
      } else if constexpr (BUS_TYPE == BUS_TYPE_QUAD) {
        this->write_cmd_addr_data(8, 0x32, 24, WDATA << 8, ptr, w * h, 4);
      } else if constexpr (BUS_TYPE == BUS_TYPE_OCTAL) {
        this->write_cmd_addr_data(0, 0, 0, 0, ptr, w * h, 8);
      }
    } else {
      for (size_t y = 0; y != h; y++) {
        if constexpr (BUS_TYPE == BUS_TYPE_SINGLE || BUS_TYPE == BUS_TYPE_SINGLE_16) {
          this->write_array(ptr, w);
        } else if constexpr (BUS_TYPE == BUS_TYPE_QUAD) {
          this->write_cmd_addr_data(8, 0x32, 24, WDATA << 8, ptr, w, 4);
        } else if constexpr (BUS_TYPE == BUS_TYPE_OCTAL) {
          this->write_cmd_addr_data(0, 0, 0, 0, ptr, w, 8);
        }
        ptr += stride;
      }
    }
  }

  /**
   * Writes a buffer to the display.
   *
   * The ptr is a pointer to the pixel data
   * The other parameters are all in pixel units.
   */
  void write_to_display_(int x_start, int y_start, int w, int h, const BUFFERTYPE *ptr, int x_offset, int y_offset,
                         int x_pad) {
    this->set_addr_window_(x_start, y_start, x_start + w - 1, y_start + h - 1);
    const int64_t te_start = this->te_wait_(y_start, w, h);  // TE patch
    this->enable();
    ptr += y_offset * (x_offset + w + x_pad) + x_offset;
    if constexpr (BUFFERPIXEL == DISPLAYPIXEL) {
      this->write_display_data_(reinterpret_cast<const uint8_t *>(ptr), w * sizeof(BUFFERTYPE), h,
                                (x_offset + w + x_pad) * sizeof(BUFFERTYPE));
    } else {
      // type conversion required, do it in chunks
      uint8_t dbuffer[DISPLAYPIXEL * 48];
      uint8_t *dptr = dbuffer;
      auto stride = x_offset + w + x_pad;  // stride in pixels
      for (size_t y = 0; y != static_cast<size_t>(h); y++) {
        for (size_t x = 0; x != static_cast<size_t>(w); x++) {
          auto color_val = ptr[y * stride + x];
          if constexpr (DISPLAYPIXEL == PIXEL_MODE_18 && BUFFERPIXEL == PIXEL_MODE_16) {
            // 16 to 18 bit conversion
            if constexpr (IS_BIG_ENDIAN) {
              *dptr++ = color_val & 0xF8;
              *dptr++ = ((color_val & 0x7) << 5) | (color_val & 0xE000) >> 11;
              *dptr++ = (color_val >> 5) & 0xF8;
            } else {
              *dptr++ = (color_val >> 8) & 0xF8;  // Blue
              *dptr++ = (color_val & 0x7E0) >> 3;
              *dptr++ = color_val << 3;
            }
          } else if constexpr (DISPLAYPIXEL == PIXEL_MODE_18 && BUFFERPIXEL == PIXEL_MODE_8) {
            // 8 bit to 18 bit conversion
            *dptr++ = color_val << 6;           // Blue
            *dptr++ = (color_val & 0x1C) << 3;  // Green
            *dptr++ = (color_val & 0xE0);       // Red
          } else if constexpr (DISPLAYPIXEL == PIXEL_MODE_16 && BUFFERPIXEL == PIXEL_MODE_8) {
            if constexpr (IS_BIG_ENDIAN) {
              *dptr++ = (color_val & 0xE0) | ((color_val & 0x1C) >> 2);
              *dptr++ = (color_val & 3) << 3;
            } else {
              *dptr++ = (color_val & 3) << 3;
              *dptr++ = (color_val & 0xE0) | ((color_val & 0x1C) >> 2);
            }
          }
          // buffer full? Flush.
          if (dptr == dbuffer + sizeof(dbuffer)) {
            this->write_display_data_(dbuffer, sizeof(dbuffer), 1, sizeof(dbuffer));
            dptr = dbuffer;
          }
        }
      }
      // flush any remaining data
      if (dptr != dbuffer) {
        this->write_display_data_(dbuffer, dptr - dbuffer, 1, dptr - dbuffer);
      }
    }
    this->disable();
    this->te_done_(te_start, y_start, w, h);  // TE patch
  }

  // TE patch: enable TE output (V-blank only) and count its rising edges
  void te_setup_() {
#ifdef USE_ESP32
    if (this->te_pin_ == nullptr)
      return;
    this->te_.sem = xSemaphoreCreateBinary();
    if (this->te_.sem == nullptr) {
      esph_log_e(TAG, "TE semaphore allocation failed, TE sync disabled");
      this->te_pin_ = nullptr;
      return;
    }
    this->te_pin_->setup();
    this->write_command_(TEON, 0x00);
    this->te_pin_->attach_interrupt(TeSync::gpio_intr, &this->te_, gpio::INTERRUPT_RISING_EDGE);
#endif
  }

#ifdef USE_ESP32
  // TE patch: scan line estimate, `us` after TE. ponytail: line time = period / HEIGHT ignores the vertical
  // porch, so this runs a few lines behind the real scan. TE_MARGIN absorbs that.
  static constexpr float TE_MARGIN = 32;
  float te_line_(int64_t us) const { return us * float(HEIGHT) / this->te_.period_us; }

  // TE patch: would writing rows y..y+h starting now cross the panel scan (tear inside the chunk)?
  // Safe if the scan stays above the chunk for the whole write (the writer is faster than the scan),
  // or it's already below the chunk and won't wrap back into it before the write ends.
  bool te_would_cross_(int y, int w, int h) const {
    const TeSync &t = this->te_;
    if (t.period_us == 0 || t.us_per_px == 0)
      return true;  // no measurements yet
    const int64_t now = esp_timer_get_time();
    if (now - t.last_us > t.period_us)
      return true;  // missed or late TE edge, position unknown
    const float s0 = this->te_line_(now - t.last_us);
    const float s1 = s0 + this->te_line_(int64_t(t.us_per_px * w * h));
    const bool ahead = s0 + TE_MARGIN < y && s1 + TE_MARGIN < y + h;
    const bool behind = s0 > y + h + TE_MARGIN && s1 + TE_MARGIN < HEIGHT + y;
    return !ahead && !behind;
  }
#endif

  // TE patch: if this write would cross the scan, block until the next TE edge (start of V-blank) or the timeout.
  // Returns the write start time.
  int64_t te_wait_(int y, int w, int h) {
#ifdef USE_ESP32
    if (this->te_pin_ == nullptr)
      return 0;
    if (this->te_would_cross_(y, w, h)) {
      const int64_t t0 = esp_timer_get_time();
      xSemaphoreTake(this->te_.sem, 0);  // drop a stale edge, so we sync to the next one
      if (xSemaphoreTake(this->te_.sem, pdMS_TO_TICKS(this->te_timeout_ms_)) != pdTRUE)
        this->te_.timeouts++;
      this->te_.waits++;
      this->te_.wait_us += esp_timer_get_time() - t0;
    }
    this->te_.start_te_us = this->te_.last_us;
    return esp_timer_get_time();
#else
    return 0;
#endif
  }

  // TE patch: stats. Estimates the scan line at the write's start and end from the last TE edge and the
  // measured TE period, and counts writes whose order relative to the scan flips (a tear inside the chunk).
  // ponytail: line time = period / HEIGHT ignores the vertical porch, so crossings are an estimate.
  void te_done_(int64_t start, int y, int w, int h) {
#ifdef USE_ESP32
    if (this->te_pin_ == nullptr)
      return;
    TeSync &t = this->te_;
    const int64_t end = esp_timer_get_time();
    t.chunks++;
    t.write_us += end - start;
    t.write_px += w * h;
    // Count writes whose order relative to the (estimated) scan flipped: a tear inside the chunk.
    if (t.period_us > 0) {
      const float s0 = this->te_line_(start - t.start_te_us), s1 = this->te_line_(end - t.start_te_us);
      if (s0 < HEIGHT && (s0 < y) != (s1 < y + h))
        t.crossings++;
    }
    if (end - t.stats_start_us < 5000000)
      return;
    const uint32_t edges = t.edges;
    if (t.stats_start_us != 0 && edges != t.stats_edges) {
      t.period_us = (end - t.stats_start_us) / (edges - t.stats_edges);
      if (t.write_px != 0)
        t.us_per_px = float(t.write_us) / t.write_px;
      esph_log_i(TAG, "TE %.1fHz | %u chunks, %u waits avg %uus, %u timeouts | write %uus per %dpx row | %u crossed scan",
                 1e6f / t.period_us, (unsigned) t.chunks, (unsigned) t.waits,
                 (unsigned) (t.waits ? t.wait_us / t.waits : 0), (unsigned) t.timeouts,
                 (unsigned) (t.write_px ? t.write_us * WIDTH / t.write_px : 0), WIDTH, (unsigned) t.crossings);
    } else if (t.stats_start_us != 0) {
      esph_log_w(TAG, "No TE edges in 5s: check te_pin");
    }
    t.stats_start_us = end;
    t.stats_edges = edges;
    t.chunks = t.waits = t.timeouts = t.crossings = 0;
    t.wait_us = t.write_us = t.write_px = 0;
#endif
  }

  /* PROPERTIES */

  // GPIO pins
  GPIOPin *reset_pin_{nullptr};
  std::vector<GPIOPin *> enable_pins_{};
  GPIOPin *dc_pin_{nullptr};
  // TE patch
  InternalGPIOPin *te_pin_{nullptr};
  uint32_t te_timeout_ms_{20};
#ifdef USE_ESP32
  TeSync te_{};
#endif

  // other properties set by configuration
  bool invert_colors_{};
  optional<uint8_t> brightness_{};
  const char *model_{"Unknown"};
  // Shared PROGMEM table
  const uint8_t *init_sequence_{nullptr};
  size_t init_sequence_len_{0};
};

/**
 * Class for MIPI SPI displays with a buffer.
 *
 * @tparam BUFFERTYPE The type of the buffer pixels, e.g. uint8_t or uint16_t
 * @tparam BUFFERPIXEL Color depth of the buffer
 * @tparam DISPLAYPIXEL Color depth of the display
 * @tparam BUS_TYPE The type of the interface bus (single, quad, octal)
 * @tparam WIDTH Width of the display in pixels
 * @tparam HEIGHT Height of the display in pixels
 * @tparam OFFSET_WIDTH The x-offset of the display in pixels
 * @tparam OFFSET_HEIGHT The y-offset of the display in pixels
 * @tparam PAD_WIDTH Additional pixels recognised by the controller after the offset and width
 * @tparam PAD_HEIGHT Additional lines recognised by the controller after the offset and width
 * @tparam MADCTL The base MADCTL value for the display, with no rotation bits set.
 * @tparam HAS_HARDWARE_ROTATION Whether the display supports hardware rotation.
 * @tparam FRACTION The fraction of the display size to use for the buffer (e.g. 4 means a 1/4 buffer).
 * @tparam ROUNDING The alignment requirement for drawing operations (e.g. 2 means that x coordinates must be even)
 */
template<typename BUFFERTYPE, PixelMode BUFFERPIXEL, bool IS_BIG_ENDIAN, PixelMode DISPLAYPIXEL, BusType BUS_TYPE,
         uint16_t WIDTH, uint16_t HEIGHT, int OFFSET_WIDTH, int OFFSET_HEIGHT, int PAD_WIDTH, int PAD_HEIGHT,
         uint16_t MADCTL, bool HAS_HARDWARE_ROTATION, int FRACTION, unsigned ROUNDING>
class MipiSpiBuffer
    : public MipiSpi<BUFFERTYPE, BUFFERPIXEL, IS_BIG_ENDIAN, DISPLAYPIXEL, BUS_TYPE, WIDTH, HEIGHT, OFFSET_WIDTH,
                     OFFSET_HEIGHT, PAD_WIDTH, PAD_HEIGHT, MADCTL, HAS_HARDWARE_ROTATION> {
 public:
  // these values define the buffer size needed to write in accordance with the chip pixel alignment
  // requirements. If the required rounding does not divide the width and height, we round up to the next multiple and
  // ignore the extra columns and rows when drawing, but use them to write to the display.
  static constexpr size_t round_buffer(size_t size) { return (size + ROUNDING - 1) / ROUNDING * ROUNDING; }

  MipiSpiBuffer() = default;

  void dump_config() override {
    MipiSpi<BUFFERTYPE, BUFFERPIXEL, IS_BIG_ENDIAN, DISPLAYPIXEL, BUS_TYPE, WIDTH, HEIGHT, OFFSET_WIDTH, OFFSET_HEIGHT,
            PAD_WIDTH, PAD_HEIGHT, MADCTL, HAS_HARDWARE_ROTATION>::dump_config();
    esph_log_config(TAG,
                    "  Rotation: %d°\n"
                    "  Buffer pixels: %d bits\n"
                    "  Buffer fraction: 1/%d\n"
                    "  Buffer bytes: %zu\n"
                    "  Draw rounding: %u",
                    this->rotation_, BUFFERPIXEL * 8, FRACTION,
                    sizeof(BUFFERTYPE) * round_buffer(WIDTH) * round_buffer(HEIGHT) / FRACTION, ROUNDING);
  }

  void setup() override {
    MipiSpi<BUFFERTYPE, BUFFERPIXEL, IS_BIG_ENDIAN, DISPLAYPIXEL, BUS_TYPE, WIDTH, HEIGHT, OFFSET_WIDTH, OFFSET_HEIGHT,
            PAD_WIDTH, PAD_HEIGHT, MADCTL, HAS_HARDWARE_ROTATION>::setup();
    RAMAllocator<BUFFERTYPE> allocator{};
    this->buffer_ = allocator.allocate(round_buffer(WIDTH) * round_buffer(HEIGHT) / FRACTION);
    if (this->buffer_ == nullptr) {
      this->mark_failed(LOG_STR("Buffer allocation failed"));
    }
  }

  void update() override {
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
    auto now = millis();
#endif
    if (this->is_failed()) {
      return;
    }
    // for updates with a small buffer, we repeatedly call the writer_ function, clipping the height to a fraction of
    // the display height,
    auto increment = (this->get_height_internal() / FRACTION / ROUNDING) * ROUNDING;
    for (this->start_line_ = 0; this->start_line_ < this->get_height_internal(); this->start_line_ = this->end_line_) {
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
      auto lap = millis();
#endif
      this->end_line_ = clamp_at_most(this->start_line_ + increment, this->get_height_internal());
      if (this->auto_clear_enabled_) {
        this->clear();
      }
      if (this->page_ != nullptr) {
        this->page_->get_writer()(*this);
      } else if (this->writer_.has_value()) {
        (*this->writer_)(*this);
      } else {
        this->test_card();
      }
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
      esph_log_v(TAG, "Drawing from line %d took %dms", this->start_line_, millis() - lap);
      lap = millis();
#endif
      if (this->x_low_ > this->x_high_ || this->y_low_ > this->y_high_)
        return;
      esph_log_v(TAG, "x_low %d, y_low %d, x_high %d, y_high %d", this->x_low_, this->y_low_, this->x_high_,
                 this->y_high_);
      // Some chips require that the drawing window be aligned on certain boundaries
      this->x_low_ = this->x_low_ / ROUNDING * ROUNDING;
      this->y_low_ = this->y_low_ / ROUNDING * ROUNDING;
      this->x_high_ = round_buffer(this->x_high_ + 1) - 1;
      this->y_high_ = clamp_at_most(round_buffer(this->y_high_ + 1) - 1, this->end_line_ - 1);
      int w = this->x_high_ - this->x_low_ + 1;
      int h = this->y_high_ - this->y_low_ + 1;
      this->write_to_display_(this->x_low_, this->y_low_, w, h, this->buffer_, this->x_low_,
                              this->y_low_ - this->start_line_,
                              round_buffer(this->get_width_internal()) - w - this->x_low_);
      // invalidate watermarks
      this->x_low_ = this->get_width_internal();
      this->y_low_ = this->get_height_internal();
      this->x_high_ = 0;
      this->y_high_ = 0;
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
      esph_log_v(TAG, "Write to display took %dms", millis() - lap);
      lap = millis();
#endif
    }
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
    esph_log_v(TAG, "Total update took %dms", millis() - now);
#endif
  }

  // Draw a pixel at the given coordinates.
  void draw_pixel_at(int x, int y, Color color) override {
    if (this->is_point_clipped(x, y))
      return;
    if constexpr (not HAS_HARDWARE_ROTATION) {
      if (this->rotation_ == display::DISPLAY_ROTATION_180_DEGREES) {
        x = WIDTH - x - 1;
        y = HEIGHT - y - 1;
      } else if (this->rotation_ == display::DISPLAY_ROTATION_90_DEGREES) {
        auto tmp = x;
        x = WIDTH - y - 1;
        y = tmp;
      } else if (this->rotation_ == display::DISPLAY_ROTATION_270_DEGREES) {
        auto tmp = y;
        y = HEIGHT - x - 1;
        x = tmp;
      }
    }
    if (x < 0 || x >= this->get_width_internal() || y < this->start_line_ || y >= this->end_line_)
      return;
    this->buffer_[(y - this->start_line_) * round_buffer(this->get_width_internal()) + x] = convert_color(color);
    if (x < this->x_low_) {
      this->x_low_ = x;
    }
    if (x > this->x_high_) {
      this->x_high_ = x;
    }
    if (y < this->y_low_) {
      this->y_low_ = y;
    }
    if (y > this->y_high_) {
      this->y_high_ = y;
    }
  }

  // Fills the display with a color.
  void fill(Color color) override {
    // If clipping is active, fall back to base implementation
    if (this->get_clipping().is_set()) {
      display::Display::fill(color);
      return;
    }

    this->x_low_ = 0;
    this->y_low_ = this->start_line_;
    this->x_high_ = this->get_width_internal() - 1;
    this->y_high_ = this->end_line_ - 1;
    std::fill_n(this->buffer_, (this->end_line_ - this->start_line_) * round_buffer(this->get_width_internal()),
                convert_color(color));
  }

 protected:
  // Rotate the coordinates to match the display orientation.

  // Convert a color to the buffer pixel format.
  static BUFFERTYPE convert_color(const Color &color) {
    if constexpr (BUFFERPIXEL == PIXEL_MODE_8) {
      return (color.red & 0xE0) | (color.g & 0xE0) >> 3 | color.b >> 6;
    } else if constexpr (BUFFERPIXEL == PIXEL_MODE_16) {
      if constexpr (IS_BIG_ENDIAN) {
        return (color.r & 0xF8) | color.g >> 5 | (color.g & 0x1C) << 11 | (color.b & 0xF8) << 5;
      } else {
        return (color.r & 0xF8) << 8 | (color.g & 0xFC) << 3 | color.b >> 3;
      }
    }
    return static_cast<BUFFERTYPE>(0);
  }

  BUFFERTYPE *buffer_{};
  uint16_t x_low_{WIDTH};
  uint16_t y_low_{HEIGHT};
  uint16_t x_high_{0};
  uint16_t y_high_{0};
  uint16_t start_line_{0};
  uint16_t end_line_{1};
};

}  // namespace esphome::mipi_spi
