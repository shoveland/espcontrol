#pragma once

// Siri-style voice assistant indicator: an animated orb at the bottom centre
// with a caption bubble showing what was heard and the reply. The voice orb
// package maps voice assistant events onto phases; this header owns the LVGL
// widgets on the top layer and their animation.

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "theme_palette.h"

namespace espcontrol::voice_orb {

enum class Phase : uint8_t { HIDDEN, LISTENING, SPEAKING, THINKING, REPLYING, ERROR };

struct Fonts {
  const lv_font_t *body{nullptr};
  const lv_font_t *small{nullptr};
};

inline constexpr lv_coord_t ORB_BOX = 210;
inline constexpr lv_coord_t GLOW_SIZE = 190;
inline constexpr lv_coord_t RING_SIZE = 150;
inline constexpr lv_coord_t CORE_SIZE = 104;
inline constexpr lv_coord_t CAPTION_WIDTH = 640;
inline constexpr uint32_t FRAME_MS = 33;
inline constexpr uint32_t ORB_LINGER_MS = 2000;
inline constexpr uint32_t CAPTION_LINGER_MS = 5000;
inline constexpr uint32_t ERROR_LINGER_MS = 2500;
inline constexpr uint32_t ERROR_COLOR = 0xE53935;
inline constexpr size_t USER_TEXT_MAX = 90;
inline constexpr size_t REPLY_TEXT_MAX = 180;

struct Orb {
  lv_obj_t *root{nullptr};
  lv_obj_t *glow{nullptr};
  lv_obj_t *ring{nullptr};
  lv_obj_t *core{nullptr};
  lv_obj_t *caption{nullptr};
  lv_obj_t *you_label{nullptr};
  lv_obj_t *reply_label{nullptr};
  lv_timer_t *timer{nullptr};
  Phase phase{Phase::HIDDEN};
  uint32_t phase_started_ms{0};
  uint32_t accent{0xFF8C00};
  float level{0.0f};
  float level_target{0.0f};
  bool orb_wanted{false};
  bool caption_wanted{false};
  uint32_t caption_hide_at_ms{0};
  uint32_t orb_hide_at_ms{0};
  int orb_opa{0};
  int caption_opa{0};
  std::function<void()> on_tap;
};

inline Orb &orb() {
  static Orb instance;
  return instance;
}

inline uint32_t mix(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int shift) {
    const float x = ((a >> shift) & 0xFF) * (1.0f - t) + ((b >> shift) & 0xFF) * t;
    return static_cast<uint32_t>(x + 0.5f) << shift;
  };
  return ch(16) | ch(8) | ch(0);
}

inline std::string clip(const std::string &text, size_t max_chars) {
  if (text.size() <= max_chars) return text;
  size_t cut = max_chars;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) cut--;  // UTF-8 boundary
  const size_t space = text.rfind(' ', cut);
  if (space != std::string::npos && space > max_chars / 2) cut = space;
  return text.substr(0, cut) + "...";
}

inline lv_obj_t *make_circle(lv_obj_t *parent, lv_coord_t size) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_size(obj, size, size);
  lv_obj_center(obj);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}

// Resize the circle rather than using a transform: a plain circle is cheap to
// draw, while a scaled one is rendered to a buffer and resampled each frame.
// Diameters are kept even so the centre never shifts by half a pixel.
inline void set_diameter(lv_obj_t *obj, lv_coord_t base, float scale) {
  int32_t d = static_cast<int32_t>(base * scale + 1.0f) & ~1;
  if (d > ORB_BOX) d = ORB_BOX;
  if (d < 2) d = 2;
  if (lv_obj_get_width(obj) == d) return;
  lv_obj_set_size(obj, d, d);
  lv_obj_center(obj);
}

inline void apply_colours() {
  Orb &o = orb();
  const ThemePalette &theme = current_theme();
  const bool error = o.phase == Phase::ERROR;
  const uint32_t base = error ? ERROR_COLOR : o.accent;
  const uint32_t bright = mix(base, 0xFFFFFF, o.phase == Phase::REPLYING ? 0.45f : 0.25f);
  lv_obj_set_style_bg_color(o.glow, lv_color_hex(base), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o.glow, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_bg_color(o.core, lv_color_hex(bright), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(o.core, lv_color_hex(base), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(o.core, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o.core, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_color(o.ring, lv_color_hex(bright), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(o.caption,
                            lv_color_hex(theme_display_color(theme.surface_primary)), LV_PART_MAIN);
  lv_obj_set_style_text_color(o.you_label, lv_color_hex(theme.text_muted), LV_PART_MAIN);
  lv_obj_set_style_text_color(o.reply_label,
                              lv_color_hex(error ? ERROR_COLOR : theme.text_primary), LV_PART_MAIN);
}

inline void set_hidden(lv_obj_t *obj, bool hidden) {
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

inline int step_towards(int current, int target, int step) {
  if (current < target) return current + step > target ? target : current + step;
  if (current > target) return current - step < target ? target : current - step;
  return current;
}

inline void tick(lv_timer_t *) {
  Orb &o = orb();
  const uint32_t now = lv_tick_get();
  if (o.orb_hide_at_ms != 0 && static_cast<int32_t>(now - o.orb_hide_at_ms) >= 0) {
    o.orb_wanted = false;
    o.orb_hide_at_ms = 0;
  }
  if (o.caption_hide_at_ms != 0 && static_cast<int32_t>(now - o.caption_hide_at_ms) >= 0) {
    o.caption_wanted = false;
    o.caption_hide_at_ms = 0;
  }

  // Ease towards the mic level by elapsed time (about a 120 ms time constant),
  // so a late or dropped frame does not make the swell lurch.
  static uint32_t last_ms = now;
  const float dt = (now - last_ms) / 1000.0f;
  last_ms = now;
  const float k = dt >= 0.5f ? 1.0f : 1.0f - expf(-dt / 0.12f);
  o.level += (o.level_target - o.level) * k;
  const float t = lv_tick_elaps(o.phase_started_ms) / 1000.0f;
  constexpr float TAU = 6.2831853f;
  float glow = 0.85f, core = 1.0f;
  switch (o.phase) {
    case Phase::LISTENING:
      glow = 0.80f + 0.08f * sinf(TAU * t / 2.4f) + 0.22f * o.level;
      core = 0.97f + 0.05f * sinf(TAU * t / 2.4f);
      break;
    case Phase::SPEAKING:
      glow = 0.86f + 0.05f * sinf(TAU * t / 0.9f) + 0.24f * o.level;
      core = 1.0f + 0.08f * o.level;
      break;
    case Phase::THINKING:
      glow = 0.80f + 0.05f * sinf(TAU * t / 1.4f);
      core = 0.92f;
      lv_arc_set_rotation(o.ring, static_cast<int32_t>(t * 220.0f) % 360);
      break;
    case Phase::REPLYING: {
      // Two out-of-step waves give a talking, uneven rhythm.
      const float talk = fabsf(sinf(TAU * t / 0.6f)) * (0.6f + 0.4f * sinf(TAU * t / 1.7f));
      glow = 0.84f + 0.14f * talk;
      core = 0.97f + 0.07f * talk;
      break;
    }
    case Phase::ERROR:
      glow = 0.8f + 0.05f * sinf(TAU * t / 0.4f);
      break;
    case Phase::HIDDEN:
      break;
  }
  set_diameter(o.glow, GLOW_SIZE, glow);
  set_diameter(o.core, CORE_SIZE, core);

  o.orb_opa = step_towards(o.orb_opa, o.orb_wanted ? 255 : 0, 32);
  o.caption_opa = step_towards(o.caption_opa, o.caption_wanted ? 255 : 0, 24);
  lv_obj_set_style_opa(o.root, o.orb_opa, LV_PART_MAIN);
  lv_obj_set_style_opa(o.caption, o.caption_opa, LV_PART_MAIN);
  set_hidden(o.root, o.orb_opa == 0);
  set_hidden(o.caption, o.caption_opa == 0);
  if (o.orb_opa == 0 && o.caption_opa == 0 && !o.orb_wanted && !o.caption_wanted) {
    o.phase = Phase::HIDDEN;
    lv_timer_pause(o.timer);
  }
}

inline void ensure_created(const Fonts &fonts) {
  Orb &o = orb();
  if (o.root) return;
  lv_obj_t *layer = lv_layer_top();

  o.root = lv_obj_create(layer);
  lv_obj_set_size(o.root, ORB_BOX, ORB_BOX);
  lv_obj_align(o.root, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_opa(o.root, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(o.root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(o.root, 0, LV_PART_MAIN);
  lv_obj_remove_flag(o.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(o.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(o.root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(o.root, [](lv_event_t *) {
    if (orb().on_tap) orb().on_tap();
  }, LV_EVENT_CLICKED, nullptr);

  o.glow = make_circle(o.root, GLOW_SIZE);
  o.ring = lv_arc_create(o.root);
  lv_obj_set_size(o.ring, RING_SIZE, RING_SIZE);
  lv_obj_center(o.ring);
  lv_obj_remove_style(o.ring, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(o.ring, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_bg_angles(o.ring, 0, 360);
  lv_arc_set_angles(o.ring, 0, 100);
  lv_obj_set_style_arc_opa(o.ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(o.ring, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(o.ring, true, LV_PART_INDICATOR);
  lv_obj_add_flag(o.ring, LV_OBJ_FLAG_HIDDEN);
  o.core = make_circle(o.root, CORE_SIZE);

  // The caption never takes touches, so the cards underneath stay usable.
  o.caption = lv_obj_create(layer);
  lv_obj_set_width(o.caption, CAPTION_WIDTH);
  lv_obj_set_height(o.caption, LV_SIZE_CONTENT);
  lv_obj_align(o.caption, LV_ALIGN_BOTTOM_MID, 0, -(ORB_BOX + 4));
  lv_obj_set_style_radius(o.caption, 24, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o.caption, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_border_width(o.caption, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(o.caption, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(o.caption, 16, LV_PART_MAIN);
  lv_obj_set_style_pad_row(o.caption, 8, LV_PART_MAIN);
  lv_obj_set_flex_flow(o.caption, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(o.caption, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o.caption, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(o.caption, LV_OBJ_FLAG_HIDDEN);
  for (lv_obj_t **label : {&o.you_label, &o.reply_label}) {
    *label = lv_label_create(o.caption);
    lv_obj_set_width(*label, LV_PCT(100));
    lv_label_set_long_mode(*label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(*label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(*label, "");
    lv_obj_add_flag(*label, LV_OBJ_FLAG_HIDDEN);
  }
  if (fonts.small) lv_obj_set_style_text_font(o.you_label, fonts.small, LV_PART_MAIN);
  if (fonts.body) lv_obj_set_style_text_font(o.reply_label, fonts.body, LV_PART_MAIN);

  o.timer = lv_timer_create(tick, FRAME_MS, nullptr);
  lv_timer_pause(o.timer);
}

inline void set_label(lv_obj_t *label, const std::string &text) {
  lv_label_set_text(label, text.c_str());
  set_hidden(label, text.empty());
}

inline void refresh_caption_visibility() {
  Orb &o = orb();
  o.caption_wanted = !lv_obj_has_flag(o.you_label, LV_OBJ_FLAG_HIDDEN) ||
                     !lv_obj_has_flag(o.reply_label, LV_OBJ_FLAG_HIDDEN);
}

// A new request clears the previous exchange.
inline void begin_interaction() {
  Orb &o = orb();
  if (!o.root) return;
  set_label(o.you_label, "");
  set_label(o.reply_label, "");
  o.caption_wanted = false;
  o.caption_hide_at_ms = 0;
  o.level = o.level_target = 0.0f;
}

inline void set_phase(Phase phase, uint32_t accent) {
  Orb &o = orb();
  if (!o.root) return;
  if (phase != o.phase) o.phase_started_ms = lv_tick_get();
  o.phase = phase;
  o.accent = accent;
  o.orb_wanted = true;
  o.orb_hide_at_ms = 0;
  set_hidden(o.ring, phase != Phase::THINKING);
  apply_colours();
  lv_obj_move_foreground(o.caption);
  lv_obj_move_foreground(o.root);
  lv_timer_resume(o.timer);
}

inline void set_level_db(float db) {
  // Map typical speech RMS (-55 dB quiet .. -20 dB loud) onto 0..1.
  float level = (db + 55.0f) / 35.0f;
  orb().level_target = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
}

inline void set_user_text(const std::string &text) {
  Orb &o = orb();
  if (!o.root) return;
  set_label(o.you_label, text.empty() ? "" : "\"" + clip(text, USER_TEXT_MAX) + "\"");
  refresh_caption_visibility();
}

inline void set_reply_text(const std::string &text) {
  Orb &o = orb();
  if (!o.root) return;
  set_label(o.reply_label, clip(text, REPLY_TEXT_MAX));
  refresh_caption_visibility();
}

// The interaction is over: the orb settles into a calm breathe and fades after
// ORB_LINGER_MS, and the caption stays readable for CAPTION_LINGER_MS.
inline void finish() {
  Orb &o = orb();
  if (!o.root || o.phase == Phase::HIDDEN) return;
  if (o.phase == Phase::ERROR) return;  // error() schedules its own fade
  if (o.orb_hide_at_ms != 0) return;    // already finishing
  const uint32_t now = lv_tick_get();
  if (o.phase != Phase::LISTENING) o.phase_started_ms = now;
  o.phase = Phase::LISTENING;
  o.level_target = 0.0f;
  set_hidden(o.ring, true);
  apply_colours();
  o.orb_hide_at_ms = now + ORB_LINGER_MS;
  o.caption_hide_at_ms = o.caption_wanted ? now + CAPTION_LINGER_MS : 0;
}

inline void error(const std::string &message, uint32_t accent) {
  Orb &o = orb();
  if (!o.root) return;
  set_phase(Phase::ERROR, accent);
  set_label(o.reply_label, message);
  refresh_caption_visibility();
  apply_colours();
  o.orb_hide_at_ms = lv_tick_get() + ERROR_LINGER_MS;
  o.caption_hide_at_ms = lv_tick_get() + ERROR_LINGER_MS;
}

}  // namespace espcontrol::voice_orb
