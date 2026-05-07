#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <M5Unified.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "image_data.h"
#include "USB.h"
#include "USBHID.h"

namespace {
constexpr int kImageWidth = 320;   // plate画像の横幅(px)
constexpr int kImageHeight = 120;  // plate画像の縦幅(px)
constexpr int kImageBytesPerRow = 40;  // 320bit / 8 = 40byte
constexpr int kStickNeutral = 128;
constexpr int kDisplayRefreshMs = 100;

// カーソル移動の挙動を調整する値はここだけ見れば変更できるように集約。
// 時間はすべてmsで直接指定する。kReportIntervalMsの倍数に切り捨てられるので注意。
namespace cursor_tuning {
// ---- 送信ループ周期 ----
constexpr int kReportIntervalMs = 5;  // ESP32側の送信ループ周期(ms)。Switchの受付窓≈8ms。

// ---- 行アンカー (通常は無効) ----
constexpr int kRowAnchorOvershootSteps = 0;  // 行アンカーのオーバーシュート量(steps)。0=無効
constexpr int kRowAnchorSettleMs = 0;  // 行アンカー後の中立待機時間(ms)

// ---- ボタン押下・スティック入力の保持時間 ----
constexpr int kStopPressHoldMs = 15;   // Aボタン押下の保持時間(ms)。kReportIntervalMsの倍数推奨
constexpr int kMoveXHoldMs = 15;       // X方向スティック入力の保持時間(ms)
constexpr int kAnchorMoveHoldMs = 15;  // アンカー移動時のスティック入力保持時間(ms)
constexpr int kMoveYHoldMs = 10;       // Y方向スティック入力の保持時間(ms)。短すぎると0px取りこぼし

// ---- Y移動前後の中立待機時間 ----
constexpr int kPostMoveXSettleMs = 0;   // X移動後・Y移動前の中立待機時間(ms)
constexpr int kPreMoveYSettleMs = 10;   // Y移動前の中立待機時間(ms)。前入力の誤検出を防ぐ
constexpr int kPostMoveYSettleMs = 10;  // Y移動後の中立待機時間(ms)。連続下入力を防ぐ

// ---- 上記ms値からフレーム数へ変換 (変更不要) ----
// Echoes = 追加再送フレーム数。実効保持時間 = (1 + Echoes) × kReportIntervalMs ms
constexpr int kRowAnchorSettleFrames = kRowAnchorSettleMs / kReportIntervalMs;
constexpr int kStopPressEchoes       = kStopPressHoldMs   / kReportIntervalMs - 1;
constexpr int kMoveXEchoes           = kMoveXHoldMs       / kReportIntervalMs - 1;
constexpr int kAnchorMoveEchoes      = kAnchorMoveHoldMs  / kReportIntervalMs - 1;
constexpr int kMoveYEchoes           = kMoveYHoldMs       / kReportIntervalMs - 1;
constexpr int kPostMoveXSettleFrames = kPostMoveXSettleMs / kReportIntervalMs;
constexpr int kPreMoveYSettleFrames  = kPreMoveYSettleMs  / kReportIntervalMs;
constexpr int kPostMoveYSettleFrames = kPostMoveYSettleMs / kReportIntervalMs;
}  // namespace cursor_tuning

// プレビューと進捗バーの位置とサイズ。Original互換のため定数にしているが、実際には描画内容に応じて動的に変えても良い。
constexpr int kPreviewX = 4;
constexpr int kPreviewY = 20;
constexpr int kPreviewHeight = 45;

constexpr int kProgressBarX = 10;
constexpr int kProgressBarY = 96;
constexpr int kProgressBarHeight = 12;
}

typedef enum {
  SWITCH_Y = 0x01,
  SWITCH_B = 0x02,
  SWITCH_A = 0x04,
  SWITCH_X = 0x08,
  SWITCH_L = 0x10,
  SWITCH_R = 0x20,
  SWITCH_ZL = 0x40,
  SWITCH_ZR = 0x80,
  SWITCH_MINUS = 0x100,
  SWITCH_PLUS = 0x200,
  SWITCH_LCLICK = 0x400,
  SWITCH_RCLICK = 0x800,
  SWITCH_HOME = 0x1000,
  SWITCH_CAPTURE = 0x2000,
} switch_button_t;

typedef enum {
  HAT_TOP = 0x00,
  HAT_TOP_RIGHT = 0x01,
  HAT_RIGHT = 0x02,
  HAT_BOTTOM_RIGHT = 0x03,
  HAT_BOTTOM = 0x04,
  HAT_BOTTOM_LEFT = 0x05,
  HAT_LEFT = 0x06,
  HAT_TOP_LEFT = 0x07,
  HAT_CENTER = 0x08,
} switch_hat_t;

typedef enum {
  STATE_SYNC_CONTROLLER,
  STATE_SYNC_POSITION,
  STATE_STOP_X,
  STATE_STOP_Y,
  STATE_MOVE_X,
  STATE_PRE_MOVE_Y_SETTLE,
  STATE_POST_MOVE_X_SETTLE,
  STATE_MOVE_Y,
  STATE_POST_MOVE_Y_SETTLE,
  STATE_REANCHOR_ROW,
  STATE_REANCHOR_SETTLE,
  STATE_DONE,
} print_state_t;

typedef enum {
  DISPLAY_WAIT_USB,
  DISPLAY_READY,
  DISPLAY_PRINTING,
  DISPLAY_DONE,
} display_state_t;

typedef struct __attribute__((packed)) {
  uint16_t button;
  uint8_t hat;
  uint8_t lx;
  uint8_t ly;
  uint8_t rx;
  uint8_t ry;
  uint8_t vendor_spec;
} switch_input_report_t;

typedef struct {
  print_state_t state;
  int echoes;  // 直前レポートを再送する残りフレーム数
  switch_input_report_t last_report;
  int report_count;  // SYNC系で使う汎用カウンタ
  int xpos;  // 現在の描画X座標
  int ypos;  // 現在の描画Y座標
  int pre_move_y_settle_frames;  // Y移動前の中立待機フレーム
  print_state_t post_move_x_next_state;
  int post_move_x_settle_frames;  // X移動後の中立待機フレーム
  int post_move_y_settle_frames;  // Y移動後の中立待機フレーム
  int row_anchor_steps;  // 行頭アンカーで端方向に押し込む残りステップ
  int row_settle_frames;  // 行頭アンカー後の待機フレーム
  bool armed;  // true: 自動描画中 / false: 待機中
} printer_state_t;

static const char *TAG = "switch_fightstick";

static const uint8_t switch_hid_report_descriptor[] = {
  0x05, 0x01,
  0x09, 0x05,
  0xA1, 0x01,
    0x15, 0x00,
    0x25, 0x01,
    0x35, 0x00,
    0x45, 0x01,
    0x75, 0x01,
    0x95, 0x10,
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x10,
    0x81, 0x02,
    0x05, 0x01,
    0x25, 0x07,
    0x46, 0x3B, 0x01,
    0x75, 0x04,
    0x95, 0x01,
    0x65, 0x14,
    0x09, 0x39,
    0x81, 0x42,
    0x65, 0x00,
    0x95, 0x01,
    0x81, 0x01,
    0x26, 0xFF, 0x00,
    0x46, 0xFF, 0x00,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x32,
    0x09, 0x35,
    0x75, 0x08,
    0x95, 0x04,
    0x81, 0x02,
    0x06, 0x00, 0xFF,
    0x09, 0x20,
    0x95, 0x01,
    0x81, 0x02,
    0x0A, 0x21, 0x26,
    0x95, 0x08,
    0x91, 0x02,
  0xC0,
};

class SwitchHIDDevice : public USBHIDDevice {
public:
  uint16_t _onGetDescriptor(uint8_t *buffer) override {
    memcpy(buffer, switch_hid_report_descriptor, sizeof(switch_hid_report_descriptor));
    return sizeof(switch_hid_report_descriptor);
  }

  void _onOutput(uint8_t report_id, const uint8_t *buffer, uint16_t len) override {
    (void)report_id;
    (void)buffer;
    (void)len;
  }
};

static SwitchHIDDevice switch_hid_device;
static USBHID switch_hid;

static printer_state_t printer_state = {};
static bool usb_mounted = false;  // Switch側でUSB HIDが列挙済みか
static bool start_requested = false;  // BtnAで開始要求が入ったか
static volatile bool force_display_refresh = false;  // 表示タスクへの強制再描画フラグ

static void reset_printer_state(void) {
  memset(&printer_state, 0, sizeof(printer_state));
  printer_state.state = STATE_SYNC_CONTROLLER;
}

static void reset_report(switch_input_report_t *report) {
  memset(report, 0, sizeof(*report));
  report->hat = HAT_CENTER;
  report->lx = kStickNeutral;
  report->ly = kStickNeutral;
  report->rx = kStickNeutral;
  report->ry = kStickNeutral;
}

static inline size_t image_index(int x, int y) {
  return (size_t)(x / 8) + ((size_t)y * kImageBytesPerRow);
}

static bool current_pixel_should_ink(void) {
  const size_t index = image_index(printer_state.xpos, printer_state.ypos);
  const uint8_t bit = (uint8_t)(1U << (printer_state.xpos % 8));
  return (image_data[index] & bit) != 0;
}

static inline void press_a_if_current_pixel_should_ink(switch_input_report_t *report) {
  if (current_pixel_should_ink()) {
    report->button |= SWITCH_A;
  }
}

static inline bool row_scans_left_to_right(void) {
  return (printer_state.ypos % 2) == 0;
}

static void begin_row_anchor(void) {
  // 偶数行は左端、奇数行は右端にアンカー
  printer_state.xpos = row_scans_left_to_right() ? 0 : (kImageWidth - 1);
  printer_state.row_anchor_steps = cursor_tuning::kRowAnchorOvershootSteps;
  printer_state.row_settle_frames = cursor_tuning::kRowAnchorSettleFrames;
  printer_state.state = STATE_REANCHOR_ROW;
}

static bool image_pixel_should_ink(int x, int y) {
  if (x < 0 || x >= kImageWidth || y < 0 || y >= kImageHeight) {
    return false;
  }
  const size_t index = image_index(x, y);
  const uint8_t bit = (uint8_t)(1U << (x % 8));
  return (image_data[index] & bit) != 0;
}

static void draw_plate_preview(int x, int y, int w, int h) {
  M5.Display.drawRect(x - 1, y - 1, w + 2, h + 2, TFT_DARKGREY);
  M5.Display.fillRect(x, y, w, h, TFT_BLACK);

  for (int dy = 0; dy < h; ++dy) {
    const int sy = (dy * kImageHeight) / h;
    for (int dx = 0; dx < w; ++dx) {
      const int sx = (dx * kImageWidth) / w;
      if (image_pixel_should_ink(sx, sy)) {
        M5.Display.drawPixel(x + dx, y + dy, TFT_WHITE);
      }
    }
  }
}

static display_state_t get_display_state(void) {
  if (!usb_mounted) {
    return DISPLAY_WAIT_USB;
  }
  if (printer_state.armed) {
    return DISPLAY_PRINTING;
  }
  if (printer_state.state == STATE_DONE) {
    return DISPLAY_DONE;
  }
  return DISPLAY_READY;
}

static void draw_progress_bar(int progress_percent) {
  const int bar_w = M5.Display.width() - 20;
  const int fill_w = (bar_w - 2) * progress_percent / 100;

  M5.Display.drawRect(kProgressBarX, kProgressBarY, bar_w, kProgressBarHeight, TFT_DARKGREY);
  M5.Display.fillRect(kProgressBarX + 1, kProgressBarY + 1, bar_w - 2, kProgressBarHeight - 2, TFT_BLACK);
  if (fill_w > 0) {
    M5.Display.fillRect(kProgressBarX + 1, kProgressBarY + 1, fill_w, kProgressBarHeight - 2, TFT_CYAN);
  }
}

static int current_progress_percent(void) {
  return ((printer_state.ypos * kImageWidth) + printer_state.xpos) * 100 / (kImageWidth * kImageHeight);
}

static void draw_wait_screen(void) {
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.println("USB not mounted");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("");
  M5.Display.println("Connect to Switch");
}

static void draw_ready_screen(int preview_w) {
  draw_plate_preview(kPreviewX, kPreviewY, preview_w, kPreviewHeight);
  M5.Display.setCursor(0, 72);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.println("Ready");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("");
  M5.Display.println("Press BtnA");
  M5.Display.println("to start");
}

static void draw_printing_screen(int preview_w) {
  char line[32];
  const int progress = current_progress_percent();

  draw_plate_preview(kPreviewX, kPreviewY, preview_w, kPreviewHeight);
  M5.Display.setCursor(0, 72);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.println("Printing");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("");
  snprintf(line, sizeof(line), "X:%03d Y:%03d", printer_state.xpos, printer_state.ypos);
  M5.Display.println(line);
  snprintf(line, sizeof(line), "%3d%%", progress);
  M5.Display.println(line);
  draw_progress_bar(progress);
}

static void draw_done_screen(int preview_w) {
  draw_plate_preview(kPreviewX, kPreviewY, preview_w, kPreviewHeight);
  M5.Display.setCursor(0, 72);
  M5.Display.setTextColor(TFT_MAGENTA, TFT_BLACK);
  M5.Display.println("Done");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("");
  M5.Display.println("Press BtnA");
  M5.Display.println("to run again");
}

static void render_display(bool force) {
  static display_state_t last_state = DISPLAY_WAIT_USB;
  static int last_xpos = -1;
  static int last_ypos = -1;
  static TickType_t last_refresh = 0;

  const display_state_t state = get_display_state();
  const TickType_t now = xTaskGetTickCount();
  const bool printing = state == DISPLAY_PRINTING;
  const bool progress_changed = printing &&
                                (printer_state.xpos != last_xpos || printer_state.ypos != last_ypos);
  const bool periodic_refresh = printing && (now - last_refresh) >= pdMS_TO_TICKS(kDisplayRefreshMs);

  if (!force && state == last_state && !progress_changed && !periodic_refresh) {
    return;
  }

  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(0, 0);
  M5.Display.println("Switch Fightstick");
  M5.Display.drawFastHLine(0, 12, M5.Display.width(), TFT_DARKGREY);
  M5.Display.setCursor(0, 20);

  const int preview_w = M5.Display.width() - 8;

  switch (state) {
    case DISPLAY_WAIT_USB:
      draw_wait_screen();
      break;

    case DISPLAY_READY:
      draw_ready_screen(preview_w);
      break;

    case DISPLAY_PRINTING:
      draw_printing_screen(preview_w);
      break;

    case DISPLAY_DONE:
      draw_done_screen(preview_w);
      break;
  }

  M5.Display.endWrite();
  last_state = state;
  last_xpos = printer_state.xpos;
  last_ypos = printer_state.ypos;
  last_refresh = now;
}

static void build_next_report(switch_input_report_t *report) {
  reset_report(report);

  if (!printer_state.armed) {
    if (start_requested) {
      ESP_LOGI(TAG, "Start requested from BtnA");
      start_requested = false;
      reset_printer_state();
      printer_state.armed = true;
    }
    return;
  }

  if (printer_state.echoes > 0) {
    memcpy(report, &printer_state.last_report, sizeof(*report));
    printer_state.echoes--;
    return;
  }

  switch (printer_state.state) {
    case STATE_SYNC_CONTROLLER:
      if (printer_state.report_count > 100) {
        printer_state.report_count = 0;
        printer_state.state = STATE_SYNC_POSITION;
      } else if (printer_state.report_count == 25 || printer_state.report_count == 50) {
        report->button |= SWITCH_L | SWITCH_R;
      } else if (printer_state.report_count == 75 || printer_state.report_count == 100) {
        report->button |= SWITCH_A;
      }
      printer_state.report_count++;
      break;

    case STATE_SYNC_POSITION:
      if (printer_state.report_count == 250) {
        printer_state.report_count = 0;
        printer_state.xpos = 0;
        printer_state.ypos = 0;
        begin_row_anchor();
      } else {
        report->lx = 0;
        report->ly = 0;
      }

      if (printer_state.report_count == 75 || printer_state.report_count == 150) {
        report->button |= SWITCH_LCLICK;
      }
      printer_state.report_count++;
      break;

    case STATE_STOP_X:
      press_a_if_current_pixel_should_ink(report);
      printer_state.state = STATE_MOVE_X;
      printer_state.echoes = cursor_tuning::kStopPressEchoes;  // A押下をエコーで確実に届ける
      break;

    case STATE_STOP_Y:
      press_a_if_current_pixel_should_ink(report);
      if (printer_state.ypos < (kImageHeight - 1)) {
        printer_state.pre_move_y_settle_frames = cursor_tuning::kPreMoveYSettleFrames;
        printer_state.state = STATE_PRE_MOVE_Y_SETTLE;
      } else {
        printer_state.state = STATE_DONE;
      }
      printer_state.echoes = cursor_tuning::kStopPressEchoes;  // A押下をエコーで確実に届ける
      break;

    case STATE_PRE_MOVE_Y_SETTLE:
      if (printer_state.pre_move_y_settle_frames > 0) {
        printer_state.pre_move_y_settle_frames--;
      }
      if (printer_state.pre_move_y_settle_frames == 0) {
        printer_state.state = STATE_MOVE_Y;
      }
      break;

    case STATE_MOVE_X:
      if (!row_scans_left_to_right()) {
        report->hat = HAT_LEFT;
        printer_state.xpos--;
      } else {
        report->hat = HAT_RIGHT;
        printer_state.xpos++;
      }

      if (printer_state.xpos > 0 && printer_state.xpos < (kImageWidth - 1)) {
        printer_state.post_move_x_next_state = STATE_STOP_X;
      } else {
        printer_state.post_move_x_next_state = STATE_STOP_Y;
      }
      printer_state.echoes = cursor_tuning::kMoveXEchoes;
      printer_state.post_move_x_settle_frames = cursor_tuning::kPostMoveXSettleFrames;
      printer_state.state = STATE_POST_MOVE_X_SETTLE;
      break;

    case STATE_POST_MOVE_X_SETTLE:
      if (printer_state.post_move_x_settle_frames > 0) {
        printer_state.post_move_x_settle_frames--;
      }
      if (printer_state.post_move_x_settle_frames == 0) {
        printer_state.state = printer_state.post_move_x_next_state;
      }
      break;

    case STATE_MOVE_Y:
      report->hat = HAT_BOTTOM;
      printer_state.ypos++;
      printer_state.echoes = cursor_tuning::kMoveYEchoes;
      printer_state.post_move_y_settle_frames = cursor_tuning::kPostMoveYSettleFrames;
      printer_state.state = STATE_POST_MOVE_Y_SETTLE;
      break;

    case STATE_POST_MOVE_Y_SETTLE:
      if (printer_state.post_move_y_settle_frames > 0) {
        printer_state.post_move_y_settle_frames--;
      }
      if (printer_state.post_move_y_settle_frames == 0) {
        begin_row_anchor();
      }
      break;

    case STATE_REANCHOR_ROW:
      report->hat = row_scans_left_to_right() ? HAT_LEFT : HAT_RIGHT;
      if (printer_state.row_anchor_steps > 0) {
        printer_state.row_anchor_steps--;
      }
      if (printer_state.row_anchor_steps == 0) {
        printer_state.state = STATE_REANCHOR_SETTLE;
      }
      printer_state.echoes = cursor_tuning::kAnchorMoveEchoes;
      break;

    case STATE_REANCHOR_SETTLE:
      if (printer_state.row_settle_frames > 0) {
        printer_state.row_settle_frames--;
      }
      if (printer_state.row_settle_frames == 0) {
        printer_state.state = STATE_STOP_X;
      }
      break;

    case STATE_DONE:
      printer_state.armed = false;
      break;
  }

  memcpy(&printer_state.last_report, report, sizeof(*report));
  // echoes は状態ごとに個別設定。
}

// 表示専用タスク: HIDタスクと分離して描画がUSB送信タイミングを邪魔しないようにする
static void display_task(void *arg) {
  (void)arg;
  while (true) {
    bool force = force_display_refresh;
    if (force) force_display_refresh = false;
    render_display(force);
    vTaskDelay(pdMS_TO_TICKS(kDisplayRefreshMs));
  }
}

static void usb_report_task(void *arg) {
  (void)arg;
  bool last_usb_mounted = false;

  while (true) {
    M5.update();
    if (M5.BtnA.wasPressed()) {
      start_requested = true;
    }

    usb_mounted = switch_hid.ready();
    if (usb_mounted != last_usb_mounted) {
      ESP_LOGI(TAG, usb_mounted ? "USB mounted" : "USB unmounted");
      start_requested = false;
      reset_printer_state();
      force_display_refresh = true;  // 表示タスクに強制更新を要求
      last_usb_mounted = usb_mounted;
    }

    const bool should_send_report = printer_state.armed || start_requested;
    if (switch_hid.ready() && should_send_report) {
      switch_input_report_t report;
      // Fix1: 送信失敗時にprinter_stateを巻き戻してレポートの欠落を防ぐ
      const printer_state_t saved_state = printer_state;
      build_next_report(&report);
      if (!switch_hid.SendReport(0, &report, sizeof(report))) {
        printer_state = saved_state;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(cursor_tuning::kReportIntervalMs));
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(0);
  M5.Display.setTextWrap(false);

  USB.VID(0x0F0D);
  USB.PID(0x0092);
  USB.manufacturerName("HORI CO.,LTD.");
  USB.productName("POKKEN CONTROLLER");
  USB.serialNumber("ATOMS3-0001");

  USBHID::addDevice(&switch_hid_device, sizeof(switch_hid_report_descriptor));
  switch_hid.begin();
  USB.begin();

  reset_printer_state();
  render_display(true);

  ESP_LOGI(TAG, "Starting USB HID (Arduino)");
  // 表示タスク: 優先度3（HIDより低く）、LCD描画がUSB送信を遅らせないようにする
  xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
  // HIDタスク: 優先度5、USB送信に専念
  xTaskCreate(usb_report_task, "switch_reports", 8192, NULL, 5, NULL);
}

void loop() {
  delay(1000);
}