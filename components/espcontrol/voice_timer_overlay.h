#pragma once

// Full-screen countdown for on-device voice timers, with a bottom pill for the
// minimised state. The voice timer package decides what to show; this header
// only owns the LVGL widgets on the top layer.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "theme_palette.h"

namespace espcontrol::voice_timer {

enum class View : uint8_t { HIDDEN, FULL, CHIP };

struct Fonts {
  const lv_font_t *digits{nullptr};
  const lv_font_t *title{nullptr};
  const lv_font_t *body{nullptr};
  const lv_font_t *small{nullptr};
  const lv_font_t *icon{nullptr};
};

// Long enough that a brush against the panel does not cancel a timer.
inline constexpr uint32_t HOLD_TO_CANCEL_MS = 1500;
inline constexpr uint32_t HOLD_FEEDBACK_MS = 400;
inline constexpr int32_t RING_SCALE = 1000;

struct Overlay {
  lv_obj_t *full{nullptr};
  lv_obj_t *ring{nullptr};
  lv_obj_t *icon{nullptr};
  lv_obj_t *title{nullptr};
  lv_obj_t *time{nullptr};
  lv_obj_t *detail{nullptr};
  lv_obj_t *hint{nullptr};
  lv_obj_t *chip{nullptr};
  lv_obj_t *chip_label{nullptr};
  View view{View::HIDDEN};
  bool ringing{false};
  std::string hint_text;
  std::string status_text;
  uint32_t status_until_ms{0};
  uint32_t press_started_ms{0};
  bool hold_fired{false};
  bool pressed{false};
  std::function<void()> on_full_tap;
  std::function<void()> on_full_hold;
  std::function<void()> on_chip_tap;
};

inline Overlay &overlay() {
  static Overlay instance;
  return instance;
}

inline constexpr const char *TIMER_ICON = "\U000F051B";  // mdi-timer-outline
inline constexpr const char *SEPARATOR = "  \xC2\xB7  ";

inline void format_remaining(char *buf, size_t size, uint32_t seconds) {
  const unsigned hours = seconds / 3600;
  const unsigned minutes = (seconds % 3600) / 60;
  const unsigned secs = seconds % 60;
  if (hours > 0) {
    snprintf(buf, size, "%u:%02u:%02u", hours, minutes, secs);
  } else {
    snprintf(buf, size, "%02u:%02u", minutes, secs);
  }
}

// Same perceived-brightness split the cards use for text on custom colours.
inline uint32_t readable_text_on(uint32_t bg) {
  const uint32_t r = (bg >> 16) & 0xFF, g = (bg >> 8) & 0xFF, b = bg & 0xFF;
  return (r * 299 + g * 587 + b * 114) / 1000 > 150 ? 0x000000 : 0xFFFFFF;
}

inline lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(parent);
  if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_text(label, "");
  return label;
}

inline void strip_container(lv_obj_t *obj) {
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// A short status (such as a cancel result) replaces the hint until it expires.
inline void refresh_hint() {
  Overlay &o = overlay();
  if (!o.hint) return;
  const bool status = !o.status_text.empty() &&
                      static_cast<int32_t>(o.status_until_ms - lv_tick_get()) > 0;
  if (!status) o.status_text.clear();
  lv_label_set_text(o.hint, status ? o.status_text.c_str() : o.hint_text.c_str());
}

inline void show_status(const std::string &text, uint32_t duration_ms) {
  Overlay &o = overlay();
  o.status_text = text;
  o.status_until_ms = lv_tick_get() + duration_ms;
  refresh_hint();
}

inline void full_event(lv_event_t *event) {
  Overlay &o = overlay();
  switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
      o.press_started_ms = lv_tick_get();
      o.hold_fired = false;
      o.pressed = true;
      break;
    case LV_EVENT_LONG_PRESSED:
    case LV_EVENT_LONG_PRESSED_REPEAT: {
      if (o.hold_fired) break;
      const uint32_t held = lv_tick_elaps(o.press_started_ms);
      if (held >= HOLD_TO_CANCEL_MS) {
        o.hold_fired = true;
        if (o.on_full_hold) o.on_full_hold();
      } else if (held >= HOLD_FEEDBACK_MS && !o.ringing && o.hint) {
        lv_label_set_text(o.hint, "Keep holding to cancel");
      }
      break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      o.pressed = false;
      if (!o.hold_fired) refresh_hint();
      break;
    case LV_EVENT_SHORT_CLICKED:
      if (o.on_full_tap) o.on_full_tap();
      break;
    default:
      break;
  }
}

inline void ensure_created(const Fonts &fonts) {
  Overlay &o = overlay();
  if (o.full) return;
  lv_obj_t *layer = lv_layer_top();

  o.full = lv_obj_create(layer);
  lv_obj_set_size(o.full, LV_PCT(100), LV_PCT(100));
  lv_obj_align(o.full, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(o.full, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o.full, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(o.full, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(o.full, 6, LV_PART_MAIN);
  lv_obj_set_flex_flow(o.full, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(o.full, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  strip_container(o.full);

  // The ring sits behind the stacked labels and empties as time runs out.
  o.ring = lv_arc_create(o.full);
  lv_obj_add_flag(o.ring, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_remove_flag(o.ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o.ring, LV_PCT(94), LV_PCT(94));
  lv_obj_center(o.ring);
  lv_obj_remove_style(o.ring, nullptr, LV_PART_KNOB);
  lv_arc_set_rotation(o.ring, 270);
  lv_arc_set_bg_angles(o.ring, 0, 360);
  lv_arc_set_range(o.ring, 0, RING_SCALE);
  lv_arc_set_value(o.ring, RING_SCALE);
  lv_obj_set_style_arc_width(o.ring, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(o.ring, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(o.ring, true, LV_PART_INDICATOR);

  o.icon = make_label(o.full, fonts.icon);
  lv_label_set_text(o.icon, TIMER_ICON);
  o.title = make_label(o.full, fonts.title);
  o.time = make_label(o.full, fonts.digits);
  o.detail = make_label(o.full, fonts.body);
  o.hint = make_label(o.full, fonts.small);
  lv_obj_set_style_pad_top(o.hint, 12, LV_PART_MAIN);
  for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_LONG_PRESSED,
                               LV_EVENT_LONG_PRESSED_REPEAT, LV_EVENT_RELEASED,
                               LV_EVENT_PRESS_LOST, LV_EVENT_SHORT_CLICKED}) {
    lv_obj_add_event_cb(o.full, full_event, code, nullptr);
  }

  // Minimised: a pill floating at the bottom centre, above the cards.
  o.chip = lv_obj_create(layer);
  lv_obj_set_size(o.chip, LV_SIZE_CONTENT, 64);
  lv_obj_align(o.chip, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_radius(o.chip, 32, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o.chip, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(o.chip, 28, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(o.chip, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(o.chip, 24, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(o.chip, LV_OPA_50, LV_PART_MAIN);
  lv_obj_remove_flag(o.chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(o.chip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(o.chip, LV_OBJ_FLAG_HIDDEN);
  o.chip_label = make_label(o.chip, fonts.body);
  lv_obj_center(o.chip_label);
  lv_obj_add_event_cb(o.chip, [](lv_event_t *) {
    if (overlay().on_chip_tap) overlay().on_chip_tap();
  }, LV_EVENT_CLICKED, nullptr);
}

// Colours are read when the view changes, so the overlay follows the active
// light/dark theme without holding a theme-refresh registration.
inline void apply_colours(uint32_t accent) {
  Overlay &o = overlay();
  if (!o.full) return;
  const ThemePalette &theme = current_theme();
  const uint32_t bg = o.ringing ? accent : theme_display_color(theme.background);
  const uint32_t fg = o.ringing ? readable_text_on(accent) : theme.text_primary;
  const uint32_t muted = o.ringing ? fg : theme.text_muted;
  lv_obj_set_style_bg_color(o.full, lv_color_hex(bg), LV_PART_MAIN);
  lv_obj_set_style_arc_color(o.ring, lv_color_hex(theme_display_color(theme.track_background)),
                             LV_PART_MAIN);
  lv_obj_set_style_arc_color(o.ring, lv_color_hex(accent), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(o.icon, lv_color_hex(o.ringing ? fg : accent), LV_PART_MAIN);
  lv_obj_set_style_text_color(o.title, lv_color_hex(fg), LV_PART_MAIN);
  lv_obj_set_style_text_color(o.time, lv_color_hex(fg), LV_PART_MAIN);
  lv_obj_set_style_text_color(o.detail, lv_color_hex(muted), LV_PART_MAIN);
  lv_obj_set_style_text_color(o.hint, lv_color_hex(muted), LV_PART_MAIN);
  lv_obj_set_style_bg_color(o.chip, lv_color_hex(accent), LV_PART_MAIN);
  lv_obj_set_style_text_color(o.chip_label, lv_color_hex(readable_text_on(accent)),
                              LV_PART_MAIN);
  if (o.ringing) {
    lv_obj_add_flag(o.ring, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(o.ring, LV_OBJ_FLAG_HIDDEN);
  }
}

inline void set_view(View view, uint32_t accent) {
  Overlay &o = overlay();
  if (!o.full) return;
  o.view = view;
  apply_colours(accent);
  if (view == View::FULL) {
    lv_obj_remove_flag(o.full, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(o.full);
  } else {
    lv_obj_add_flag(o.full, LV_OBJ_FLAG_HIDDEN);
  }
  if (view == View::CHIP) {
    lv_obj_remove_flag(o.chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(o.chip);
  } else {
    lv_obj_add_flag(o.chip, LV_OBJ_FLAG_HIDDEN);
  }
}

inline std::string others_text(int others) {
  if (others <= 0) return "";
  char buf[32];
  snprintf(buf, sizeof(buf), "+%d more timer%s", others, others == 1 ? "" : "s");
  return buf;
}

// Running (or paused) countdown for the timer that finishes soonest.
inline void show_countdown(const std::string &name, uint32_t seconds_left,
                           uint32_t total_seconds, bool active, int others) {
  Overlay &o = overlay();
  if (!o.full) return;
  o.ringing = false;
  const char *title = name.empty() ? "Timer" : name.c_str();
  char time_buf[16];
  format_remaining(time_buf, sizeof(time_buf), seconds_left);
  lv_label_set_text(o.title, title);
  lv_label_set_text(o.time, time_buf);
  std::string detail = active ? others_text(others) : "Paused";
  if (!active && others > 0) detail += SEPARATOR + others_text(others);
  lv_label_set_text(o.detail, detail.c_str());
  lv_arc_set_value(o.ring, total_seconds == 0 ? RING_SCALE
      : static_cast<int32_t>(static_cast<uint64_t>(seconds_left) * RING_SCALE / total_seconds));
  o.hint_text = std::string("Tap to minimise") + SEPARATOR + "Hold to cancel";
  // Leave "Keep holding to cancel" in place while a finger is down.
  if (!o.pressed) refresh_hint();
  char chip_buf[64];
  snprintf(chip_buf, sizeof(chip_buf), "%s  %s%s", title, time_buf, others > 0 ? "  +" : "");
  lv_label_set_text(o.chip_label, chip_buf);
}

inline void show_ringing(const std::string &name, int others) {
  Overlay &o = overlay();
  if (!o.full) return;
  o.ringing = true;
  o.status_text.clear();
  lv_label_set_text(o.title, name.empty() ? "Timer" : name.c_str());
  lv_label_set_text(o.time, "00:00");
  std::string detail = "Time's up";
  if (others > 0) detail += SEPARATOR + others_text(others);
  lv_label_set_text(o.detail, detail.c_str());
  o.hint_text = "Tap to stop";
  refresh_hint();
}

}  // namespace espcontrol::voice_timer
