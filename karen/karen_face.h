#pragma once
// Karen's animated face: vector eyes drawn with plain LVGL objects.

#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "application.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

enum KarenFace : int {
    FACE_NEUTRAL = 0, FACE_LISTENING, FACE_THINKING, FACE_HAPPY, FACE_LAUGHING, FACE_SASSY,
    FACE_ANGRY, FACE_SAD, FACE_CRYING, FACE_SURPRISED, FACE_LOVING, FACE_EMBARRASSED,
    FACE_WINKING, FACE_CONFUSED, FACE_COOL,
    // sensor reactions
    FACE_HELLO, FACE_PETTED, FACE_DIZZY, FACE_SCARED, FACE_SLEEPY, FACE_LOWBAT,
};

class KarenDisplay : public SpiLcdDisplay {
public:
    KarenDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, int w, int h,
                 int ox, int oy, bool mx, bool my, bool sxy)
        : SpiLcdDisplay(io, panel, w, h, ox, oy, mx, my, sxy) {}

    void SetupUI() override {
        if (setup_ui_called_) return;
        SpiLcdDisplay::SetupUI();
        auto* dark = LvglThemeManager::GetInstance().GetTheme("dark");
        if (dark != nullptr) SetTheme(dark);
        DisplayLockGuard lock(this);
        BuildFace();
        timer_ = lv_timer_create(TimerCb, 50, this);
    }

    bool SetEmotion(const char* emotion) override {
        if (emotion == nullptr) return true;
        emotion_.store(MapEmotion(emotion));
        return true;
    }

    // Temporary reaction from a sensor (takes priority over conversation emotion)
    void Trigger(KarenFace face, int duration_ms) {
        override_face_.store(face);
        override_until_us_.store(esp_timer_get_time() + (int64_t)duration_ms * 1000);
    }

private:
    static constexpr uint32_t PINK = 0xff5fae, RED = 0xff4054, BLUE = 0x5fc8ff, BLUSH = 0xff7fb6,
                              HEART = 0xff4f8b, YELLOW = 0xffd84f, BLACK = 0x000000, WHITE = 0xffffff;
    static constexpr float CX_L = 74, CX_R = 166, CY = 126;

    std::atomic<int> emotion_{FACE_NEUTRAL};
    std::atomic<int> override_face_{FACE_NEUTRAL};
    std::atomic<int64_t> override_until_us_{0};
    lv_timer_t* timer_ = nullptr;

    lv_obj_t* face_ = nullptr;
    lv_obj_t* eye_[2]{};
    lv_obj_t* lid_[2]{};
    lv_obj_t* cheekcut_[2]{};   // black ellipse below the eye -> happy ^ ^
    lv_obj_t* lowcut_[2]{};     // black rect from below -> squint
    lv_obj_t* pupil_[2]{};
    lv_obj_t* heart_[2][3]{};
    lv_obj_t* tear_[3]{};
    lv_obj_t* blush_[2]{};
    lv_obj_t* dot_[3]{};
    lv_obj_t* spiral_[2][7]{};
    lv_obj_t* glass_[3]{};
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* bat_[3]{};
    lv_obj_t* text_ = nullptr;
    std::string cur_text_;

    struct Eye {
        float dx = 0, dy = 0, w = 58, h = 82, r = 18;
        uint32_t col = PINK;
        float top = 0, tilt = 0, happy = 0, low = 0, blink = 0;
        bool pupil = false; float px = 0, py = 0, pr = 0;
        bool hidden = false;
    };

    static int MapEmotion(const char* e) {
        struct { const char* k; int f; } map[] = {
            {"neutral", FACE_NEUTRAL}, {"idle", FACE_NEUTRAL}, {"robot_2", FACE_NEUTRAL},
            {"happy", FACE_HAPPY}, {"relaxed", FACE_HAPPY}, {"delicious", FACE_HAPPY},
            {"laughing", FACE_LAUGHING}, {"funny", FACE_SASSY}, {"silly", FACE_SASSY},
            {"angry", FACE_ANGRY}, {"sad", FACE_SAD}, {"crying", FACE_CRYING},
            {"surprised", FACE_SURPRISED}, {"shocked", FACE_SURPRISED},
            {"loving", FACE_LOVING}, {"kissy", FACE_LOVING},
            {"embarrassed", FACE_EMBARRASSED}, {"winking", FACE_WINKING},
            {"confused", FACE_CONFUSED}, {"thinking", FACE_THINKING},
            {"cool", FACE_COOL}, {"confident", FACE_COOL}, {"sleepy", FACE_SLEEPY},
        };
        for (auto& m : map) if (strcmp(m.k, e) == 0) return m.f;
        return FACE_NEUTRAL;
    }

    static lv_obj_t* Mk(lv_obj_t* parent, uint32_t col, int32_t radius) {
        lv_obj_t* o = lv_obj_create(parent);
        lv_obj_remove_style_all(o);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
        lv_obj_set_style_radius(o, radius, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        return o;
    }
    static void Hide(lv_obj_t* o) { lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }
    static void Show(lv_obj_t* o) { lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); }
    static void Box(lv_obj_t* o, float x, float y, float w, float h) {
        lv_obj_set_pos(o, (int32_t)lroundf(x), (int32_t)lroundf(y));
        lv_obj_set_size(o, (int32_t)lroundf(w), (int32_t)lroundf(h));
        Show(o);
    }
    static void Center(lv_obj_t* o, float cx, float cy, float w, float h) { Box(o, cx - w / 2, cy - h / 2, w, h); }
    static void Color(lv_obj_t* o, uint32_t c) { lv_obj_set_style_bg_color(o, lv_color_hex(c), 0); }

    void BuildFace() {
        lv_obj_t* screen = lv_screen_active();
        face_ = lv_obj_create(screen);
        lv_obj_remove_style_all(face_);
        lv_obj_set_size(face_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(face_, 0, 0);
        lv_obj_set_style_bg_opa(face_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(face_, lv_color_hex(BLACK), 0);
        lv_obj_remove_flag(face_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(face_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_to_index(face_, 2);   // above background + emoji box, below status bars

        for (int i = 0; i < 2; i++) eye_[i] = Mk(face_, PINK, 18);
        for (int i = 0; i < 2; i++) {
            lid_[i] = Mk(face_, BLACK, 0);
            cheekcut_[i] = Mk(face_, BLACK, LV_RADIUS_CIRCLE);
            lowcut_[i] = Mk(face_, BLACK, 0);
            pupil_[i] = Mk(face_, BLACK, LV_RADIUS_CIRCLE);
        }
        for (int i = 0; i < 2; i++) {
            heart_[i][0] = Mk(face_, HEART, LV_RADIUS_CIRCLE);
            heart_[i][1] = Mk(face_, HEART, LV_RADIUS_CIRCLE);
            heart_[i][2] = Mk(face_, HEART, 3);
            lv_obj_set_style_transform_rotation(heart_[i][2], 450, 0);
        }
        for (auto& t : tear_) t = Mk(face_, BLUE, LV_RADIUS_CIRCLE);
        for (auto& b : blush_) { b = Mk(face_, BLUSH, LV_RADIUS_CIRCLE); lv_obj_set_style_bg_opa(b, 190, 0); }
        for (auto& d : dot_) d = Mk(face_, PINK, LV_RADIUS_CIRCLE);
        for (int i = 0; i < 2; i++) for (int j = 0; j < 7; j++) spiral_[i][j] = Mk(face_, PINK, LV_RADIUS_CIRCLE);
        for (int i = 0; i < 2; i++) {
            glass_[i] = Mk(face_, 0x111111, 14);
            lv_obj_set_style_border_width(glass_[i], 3, 0);
            lv_obj_set_style_border_color(glass_[i], lv_color_hex(PINK), 0);
        }
        glass_[2] = Mk(face_, PINK, 2);
        mouth_ = Mk(face_, PINK, 3);
        bat_[0] = Mk(face_, BLACK, 4);
        lv_obj_set_style_border_width(bat_[0], 3, 0);
        lv_obj_set_style_border_color(bat_[0], lv_color_hex(RED), 0);
        bat_[1] = Mk(face_, RED, 1);
        bat_[2] = Mk(face_, RED, 2);

        text_ = lv_label_create(face_);
        lv_obj_set_style_text_color(text_, lv_color_hex(PINK), 0);
        lv_obj_set_style_transform_scale_x(text_, 512, 0);
        lv_obj_set_style_transform_scale_y(text_, 512, 0);
        lv_label_set_text(text_, "");
        Hide(text_);
    }

    void Text(const char* s, float cx, float cy, uint32_t col) {
        if (cur_text_ != s) { lv_label_set_text(text_, s); cur_text_ = s; }
        lv_obj_set_style_text_color(text_, lv_color_hex(col), 0);
        lv_obj_update_layout(text_);
        int32_t w = lv_obj_get_width(text_), h = lv_obj_get_height(text_);
        lv_obj_set_style_transform_pivot_x(text_, w / 2, 0);
        lv_obj_set_style_transform_pivot_y(text_, h / 2, 0);
        lv_obj_set_pos(text_, (int32_t)(cx - w / 2), (int32_t)(cy - h / 2));
        Show(text_);
    }

    void ApplyEye(int i, float cx, float side, const Eye& e) {
        if (e.hidden) { Hide(eye_[i]); Hide(lid_[i]); Hide(cheekcut_[i]); Hide(lowcut_[i]); Hide(pupil_[i]); return; }
        float h = std::max(4.0f, e.h * (1.0f - e.blink));
        float w = e.w;
        float x = cx + e.dx - w / 2, y = CY + e.dy - h / 2;
        Box(eye_[i], x, y, w, h);
        Color(eye_[i], e.col);
        lv_obj_set_style_radius(eye_[i], (int32_t)std::min(e.r, std::min(w, h) / 2), 0);

        if (e.top > 0 || e.tilt != 0) {
            float W = w + 44, H = 80;
            float line = y + h * e.top;
            Box(lid_[i], cx + e.dx - W / 2, line - H, W, H);
            lv_obj_set_style_transform_pivot_x(lid_[i], (int32_t)(W / 2), 0);
            lv_obj_set_style_transform_pivot_y(lid_[i], (int32_t)H, 0);
            float deg = side * atan2f(2 * e.tilt, w) * 180.0f / 3.14159f;
            lv_obj_set_style_transform_rotation(lid_[i], (int32_t)(deg * 10), 0);
        } else Hide(lid_[i]);

        if (e.happy > 0) Center(cheekcut_[i], cx + e.dx, y + h + h * 0.15f, w * 1.7f, h * 1.24f * e.happy + 4);
        else Hide(cheekcut_[i]);
        if (e.low > 0) Box(lowcut_[i], x - 4, y + h * (1 - e.low), w + 8, h * e.low + 4);
        else Hide(lowcut_[i]);
        if (e.pupil) Center(pupil_[i], cx + e.dx + e.px, CY + e.dy + e.py, e.pr * 2, e.pr * 2);
        else Hide(pupil_[i]);
    }

    void Heart(int i, float cx, float cy, float s) {
        float d = s * 0.58f;
        Center(heart_[i][0], cx - s * 0.24f, cy - s * 0.12f, d, d);
        Center(heart_[i][1], cx + s * 0.24f, cy - s * 0.12f, d, d);
        float q = s * 0.56f;
        Center(heart_[i][2], cx, cy + s * 0.14f, q, q);
        lv_obj_set_style_transform_pivot_x(heart_[i][2], (int32_t)(q / 2), 0);
        lv_obj_set_style_transform_pivot_y(heart_[i][2], (int32_t)(q / 2), 0);
    }

    static float Blink(float t, float period, float phase) {
        float p = fmodf(t + phase, period);
        return p < 0.16f ? sinf(p / 0.16f * 3.14159f) : 0.0f;
    }

    int Effective() {
        int64_t now = esp_timer_get_time();
        if (now < override_until_us_.load()) return override_face_.load();
        int e = emotion_.load();
        auto st = Application::GetInstance().GetDeviceState();
        if (e == FACE_NEUTRAL && st == kDeviceStateListening) return FACE_LISTENING;
        if (e == FACE_NEUTRAL && st == kDeviceStateConnecting) return FACE_THINKING;
        return e;
    }

    static void TimerCb(lv_timer_t* t) {
        auto* self = static_cast<KarenDisplay*>(lv_timer_get_user_data(t));
        if (self) self->Frame();
    }

    void HideExtras() {
        for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) Hide(heart_[i][j]);
        for (auto o : tear_) Hide(o);
        for (auto o : blush_) Hide(o);
        for (auto o : dot_) Hide(o);
        for (int i = 0; i < 2; i++) for (int j = 0; j < 7; j++) Hide(spiral_[i][j]);
        for (auto o : glass_) Hide(o);
        for (auto o : bat_) Hide(o);
        Hide(mouth_);
        Hide(text_);
    }

    void Frame() {
        auto st = Application::GetInstance().GetDeviceState();
        bool system_screen = st == kDeviceStateStarting || st == kDeviceStateWifiConfiguring ||
                             st == kDeviceStateActivating || st == kDeviceStateUpgrading ||
                             st == kDeviceStateFatalError;
        if (system_screen) { Hide(face_); return; }
        Show(face_);

        float t = esp_timer_get_time() / 1e6f;
        int f = Effective();
        Eye L, R;
        HideExtras();
        auto both = [&](const Eye& e) { L = e; R = e; };

        switch (f) {
        case FACE_NEUTRAL: {
            Eye e; e.dx = roundf(sinf(t * 0.7f) * 1.2f) * 10; e.blink = Blink(t, 3.4f, 0); both(e); break; }
        case FACE_LISTENING: {
            float lv = (sinf(t * 9) * 0.5f + 0.5f) * (sinf(t * 2.3f) * 0.5f + 0.5f);
            Eye e; e.h = 82 + lv * 16; e.w = 58 + lv * 4; both(e);
            for (int i = 0; i < 3; i++) { float s = 8 + ((i == 1) ? lv * 6 : lv * 3); Center(dot_[i], 104 + i * 16, 214, s, s); }
            break; }
        case FACE_THINKING: {
            Eye e; e.dx = 8; e.dy = -14; e.h = 70; e.top = 0.1f; L = e; e.h = 62; R = e;
            for (int i = 0; i < 3; i++) {
                float a = sinf(t * 5 - i * 0.8f) * 0.5f + 0.5f;
                lv_obj_set_style_bg_opa(dot_[i], (lv_opa_t)(80 + a * 175), 0);
                Center(dot_[i], 150 + i * 16, 214, 10, 10);
            }
            break; }
        case FACE_HAPPY: { Eye e; e.happy = 1; both(e); break; }
        case FACE_LAUGHING: {
            float b = fabsf(sinf(t * 10)) * 8; Eye e; e.happy = 1; e.dy = -b; both(e);
            Text("HA HA", 120, 214, PINK); break; }
        case FACE_SASSY: {
            float p = fmodf(t, 4) / 4; float up = p < 0.5f ? sinf(p * 2 * 3.14159f) : 0;
            Eye e; e.top = 0.45f; e.tilt = -3; e.dy = -up * 6; both(e);
            Box(mouth_, 98, 210, 44, 5); Text("ugh", 196, 46, PINK); break; }
        case FACE_ANGRY: {
            float j = sinf(t * 40) * 1.6f; Eye e; e.tilt = 16; e.top = 0.18f; e.col = RED; e.dx = j; both(e); break; }
        case FACE_SAD: {
            Eye e; e.tilt = -14; e.top = 0.14f; e.dy = 6; e.h = 74; e.blink = Blink(t, 5, 1); both(e); break; }
        case FACE_CRYING: {
            Eye e; e.tilt = -14; e.top = 0.14f; e.dy = 6; e.h = 74; both(e);
            const float xs[3] = {56, 184, 60}, ph[3] = {0, 0.5f, 0.3f};
            for (int i = 0; i < 3; i++) {
                float q = fmodf(t * 0.9f + ph[i], 1.0f);
                lv_obj_set_style_bg_opa(tear_[i], (lv_opa_t)((1 - q) * 255), 0);
                Center(tear_[i], xs[i], 166 + q * 60, 10, 14);
            }
            break; }
        case FACE_SURPRISED: {
            float p = 1 + std::min(1.0f, fmodf(t, 3) * 3) * 0.15f;
            Eye e; e.w = 64 * p; e.h = 64 * p; e.r = 40; e.dy = -4; both(e);
            Center(mouth_, 120, 210, 18, 22); lv_obj_set_style_radius(mouth_, LV_RADIUS_CIRCLE, 0); break; }
        case FACE_LOVING: {
            L.hidden = R.hidden = true;
            float p = 1 + fabsf(sinf(t * 4)) * 0.18f;
            Heart(0, CX_L, CY, 64 * p); Heart(1, CX_R, CY, 64 * p); break; }
        case FACE_EMBARRASSED: {
            Eye e; e.happy = 1; e.dx = -6; both(e);
            Center(blush_[0], 60, 178, 44, 18); Center(blush_[1], 180, 178, 44, 18);
            Center(tear_[0], 212, 52 + fmodf(t * 12, 18), 10, 14); lv_obj_set_style_bg_opa(tear_[0], 255, 0); break; }
        case FACE_WINKING: {
            bool w = fmodf(t, 2.4f) < 1.4f; L = Eye(); Eye r; if (w) { r.happy = 1; r.h = 60; r.dy = 6; } R = r; break; }
        case FACE_CONFUSED: {
            Eye l; l.h = 66; l.w = 52; l.top = 0.12f; l.tilt = -6; L = l; Eye r; r.h = 90; r.w = 62; R = r;
            Text("?", 210, 52, PINK); break; }
        case FACE_COOL: {
            both(Eye());
            float sl = std::min(1.0f, fmodf(t, 4) / 1.2f); float y = CY - 80 + sl * 80;
            Box(glass_[0], 38, y - 36, 72, 58); Box(glass_[1], 130, y - 36, 72, 58); Box(glass_[2], 108, y - 22, 24, 4);
            Box(mouth_, 100, 206, 46, 5); break; }
        case FACE_HELLO: {
            float b = fabsf(sinf(t * 6)) * 6; Eye e; e.happy = 1; e.dy = -b; both(e);
            Text("HI!", 120, 212, PINK); break; }
        case FACE_PETTED: {
            Eye e; e.happy = 1; e.h = 70; both(e);
            Center(blush_[0], 60, 172, 40, 16); Center(blush_[1], 180, 172, 40, 16);
            float p = fmodf(t * 0.6f, 1.0f);
            for (int k = 0; k < 2; k++) {  // hearts float up from the eyes
                Heart(k, k ? 200 : 40, 70 - p * 40, 22);
            }
            L.hidden = false; R.hidden = false; break; }
        case FACE_DIZZY: {
            L.hidden = R.hidden = true;
            for (int k = 0; k < 2; k++) {
                float cx = k ? CX_R : CX_L, dir = k ? -1.0f : 1.0f;
                for (int j = 0; j < 7; j++) {
                    float a = j * 0.9f * dir + t * 6 * dir, r = 6 + j * 5.5f, s = 7 + j * 0.6f;
                    Center(spiral_[k][j], cx + cosf(a) * r, CY + sinf(a) * r, s, s);
                }
            }
            Text("*  *", 120 + sinf(t * 2) * 40, 40, YELLOW); break; }
        case FACE_SCARED: {
            float j = sinf(t * 55) * 2.5f;
            Eye e; e.w = 52; e.h = 52; e.r = 30; e.dx = j; e.pupil = true; e.pr = 7; both(e);
            Text("!", 120, 44, YELLOW); Box(mouth_, 96, 206, 48, 5); break; }
        case FACE_SLEEPY: {
            float p = fmodf(t, 6) / 6; float lid = p < 0.7f ? 0.45f + p * 0.7f : 0.94f;
            Eye e; e.top = std::min(0.94f, lid); e.dy = 4; both(e);
            float q = fmodf(t * 0.5f, 1.0f);
            Text("z Z", 184 + q * 30, 64 - q * 40, PINK); break; }
        case FACE_LOWBAT: {
            Eye e; e.top = 0.4f; e.tilt = -6; e.h = 70; e.dy = 10; both(e);
            Box(bat_[0], 96, 30, 42, 22); Box(bat_[1], 138, 36, 5, 10);
            if (fmodf(t, 1.0f) < 0.6f) Box(bat_[2], 101, 35, 8, 12);
            Box(mouth_, 104, 208, 32, 5); break; }
        default: both(Eye()); break;
        }
        if (f != FACE_SURPRISED) lv_obj_set_style_radius(mouth_, 3, 0);
        if (f != FACE_THINKING) for (auto d : dot_) lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        ApplyEye(0, CX_L, 1.0f, L);
        ApplyEye(1, CX_R, -1.0f, R);
    }
};
