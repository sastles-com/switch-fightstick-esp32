#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <WebServer.h>
#include <WiFi.h>
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
constexpr int kImageDataSize = 0x12c1;
constexpr int kStickNeutral = 128;
constexpr int kDisplayRefreshMs = 100;
constexpr int kBtnALongPressMs = 2000;
constexpr const char *kCustomImagePath = "/image.bin";
constexpr const char *kUploadTempPath = "/image.tmp";
constexpr const char *kApSsid = "AtomS3-ImageUI";
constexpr const char *kApPassword = "12345678";

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
  DISPLAY_WEB_QR,
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
static WebServer web_server(80);
static DNSServer dns_server;

static printer_state_t printer_state = {};
static bool usb_mounted = false;  // Switch側でUSB HIDが列挙済みか
static bool start_requested = false;  // BtnAで開始要求が入ったか
static volatile bool force_display_refresh = false;  // 表示タスクへの強制再描画フラグ
static volatile bool btn_a_being_held_for_qr = false;  // 長押しQRモード用: ボタン押下中で長押し判定前
static bool show_web_qr = false;  // true: BtnA長押しでWeb接続QRを表示
static uint32_t btn_a_press_start_ms = 0;
static bool btn_a_long_press_handled = false;
static uint8_t custom_image_data[kImageDataSize] = {};
static const uint8_t *active_image_data = image_data;
static bool custom_image_loaded = false;
static File upload_file;
static size_t upload_received_bytes = 0;
static bool upload_failed = false;
static String upload_error_message;

typedef struct {
  int report_interval_ms;
  int stop_press_hold_ms;
  int move_x_hold_ms;
  int anchor_move_hold_ms;
  int move_y_hold_ms;
} runtime_tuning_t;

static runtime_tuning_t runtime_tuning = {
  cursor_tuning::kReportIntervalMs,
  cursor_tuning::kStopPressHoldMs,
  cursor_tuning::kMoveXHoldMs,
  cursor_tuning::kAnchorMoveHoldMs,
  cursor_tuning::kMoveYHoldMs,
};

static int clamp_int(int v, int min_v, int max_v) {
  if (v < min_v) return min_v;
  if (v > max_v) return max_v;
  return v;
}

static int tuning_interval_ms(void) {
  return clamp_int(runtime_tuning.report_interval_ms, 1, 33);
}

static int tuning_echoes_from_hold_ms(int hold_ms) {
  const int interval_ms = tuning_interval_ms();
  const int echoes = (hold_ms / interval_ms) - 1;
  return echoes > 0 ? echoes : 0;
}

static int tuning_frames_from_ms(int wait_ms) {
  const int interval_ms = tuning_interval_ms();
  const int frames = wait_ms / interval_ms;
  return frames > 0 ? frames : 0;
}

static int tuning_stop_press_echoes(void) {
  return tuning_echoes_from_hold_ms(clamp_int(runtime_tuning.stop_press_hold_ms, 1, 200));
}

static int tuning_move_x_echoes(void) {
  return tuning_echoes_from_hold_ms(clamp_int(runtime_tuning.move_x_hold_ms, 1, 200));
}

static int tuning_anchor_move_echoes(void) {
  return tuning_echoes_from_hold_ms(clamp_int(runtime_tuning.anchor_move_hold_ms, 1, 200));
}

static int tuning_move_y_echoes(void) {
  return tuning_echoes_from_hold_ms(clamp_int(runtime_tuning.move_y_hold_ms, 1, 200));
}

static int tuning_row_anchor_settle_frames(void) {
  return tuning_frames_from_ms(cursor_tuning::kRowAnchorSettleMs);
}

static int tuning_pre_move_y_settle_frames(void) {
  return tuning_frames_from_ms(cursor_tuning::kPreMoveYSettleMs);
}

static int tuning_post_move_x_settle_frames(void) {
  return tuning_frames_from_ms(cursor_tuning::kPostMoveXSettleMs);
}

static int tuning_post_move_y_settle_frames(void) {
  return tuning_frames_from_ms(cursor_tuning::kPostMoveYSettleMs);
}

static const char kWebUiHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="ja">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>AtomS3 Image Uploader</title>
  <style>
    :root { color-scheme: light; }
    body { font-family: sans-serif; margin: 20px; max-width: 900px; }
    .row { margin-bottom: 12px; }
    button { padding: 8px 14px; margin-right: 8px; }
    .danger {
      background: #c62828;
      color: #fff;
      border: 1px solid #8e0000;
      font-weight: 700;
    }
    .danger:hover { background: #b71c1c; }
    .hint { color: #666; font-size: 13px; }
    canvas { border: 1px solid #555; image-rendering: pixelated; width: 640px; height: 240px; max-width: 100%; }
    .mono { font-family: ui-monospace, monospace; }
  </style>
</head>
<body>
  <h1>AtomS3 Image Uploader</h1>
  <p>320x120にリサイズして1bit化し、デバイスへ保存します。</p>

  <div class="row">
    <input id="file" type="file" accept="image/png,image/jpeg,image/webp,image/bmp">
  </div>

  <div class="row">
    <label>Threshold: <span id="thv">128</span></label>
    <input id="threshold" type="range" min="0" max="255" value="128">
    <label style="margin-left:12px;"><input id="invert" type="checkbox"> Invert</label>
    <label style="margin-left:12px;"><input id="dither" type="checkbox" checked> Dither</label>
  </div>

  <div class="row">
    <button id="upload" disabled>Upload to AtomS3</button>
    <button id="clear">Use built-in image</button>
  </div>

  <h2>Tuning</h2>
  <div class="row">
    <label>Report interval(ms): <input id="reportIntervalMs" type="number" min="1" max="33" value="5" style="width:72px"></label>
  </div>
  <div class="row">
    <label>Stop press hold(ms): <input id="stopPressHoldMs" type="number" min="1" max="200" value="15" style="width:72px"></label>
    <label style="margin-left:12px;">Move X hold(ms): <input id="moveXHoldMs" type="number" min="1" max="200" value="15" style="width:72px"></label>
  </div>
  <div class="row">
    <label>Anchor hold(ms): <input id="anchorMoveHoldMs" type="number" min="1" max="200" value="15" style="width:72px"></label>
    <label style="margin-left:12px;">Move Y hold(ms): <input id="moveYHoldMs" type="number" min="1" max="200" value="10" style="width:72px"></label>
  </div>
  <div class="row">
    <button id="saveTuning">Save Tuning</button>
    <button id="done" class="danger">完了して再起動</button>
  </div>
  <div class="row hint">※ 設定反映後に押すと、デバイスを再起動してQRモードを終了します。</div>

  <div class="row">
    <canvas id="preview" width="320" height="120"></canvas>
  </div>

  <div class="row mono" id="status">Status: waiting file...</div>

  <script>
    const fileInput = document.getElementById('file');
    const threshold = document.getElementById('threshold');
    const thv = document.getElementById('thv');
    const invert = document.getElementById('invert');
    const dither = document.getElementById('dither');
    const uploadBtn = document.getElementById('upload');
    const clearBtn = document.getElementById('clear');
    const saveTuningBtn = document.getElementById('saveTuning');
    const doneBtn = document.getElementById('done');
    const statusEl = document.getElementById('status');
    const canvas = document.getElementById('preview');
    const ctx = canvas.getContext('2d', { willReadFrequently: true });

    const reportIntervalMs = document.getElementById('reportIntervalMs');
    const stopPressHoldMs = document.getElementById('stopPressHoldMs');
    const moveXHoldMs = document.getElementById('moveXHoldMs');
    const anchorMoveHoldMs = document.getElementById('anchorMoveHoldMs');
    const moveYHoldMs = document.getElementById('moveYHoldMs');

    let loadedImage = null;
    let doneArmedUntil = 0;

    function setStatus(text) {
      statusEl.textContent = 'Status: ' + text;
    }

    function renderPreview() {
      if (!loadedImage) {
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, 320, 120);
        uploadBtn.disabled = true;
        return;
      }

      ctx.drawImage(loadedImage, 0, 0, 320, 120);
      const img = ctx.getImageData(0, 0, 320, 120);
      const data = img.data;
      const th = Number(threshold.value);
      const inv = invert.checked;

      if (dither.checked) {
        // Floyd-Steinberg error diffusion (png2c.py の convert("1") に近い見た目)
        const lum = new Float32Array(320 * 120);
        for (let i = 0, p = 0; i < data.length; i += 4, p++) {
          lum[p] = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
        }

        for (let y = 0; y < 120; y++) {
          for (let x = 0; x < 320; x++) {
            const idx = y * 320 + x;
            const oldVal = lum[idx];
            let on = oldVal < th;
            if (inv) on = !on;
            const newVal = on ? 255 : 0;
            const err = oldVal - newVal;
            lum[idx] = newVal;

            if (x + 1 < 320) lum[idx + 1] += err * (7 / 16);
            if (y + 1 < 120) {
              if (x > 0) lum[idx + 320 - 1] += err * (3 / 16);
              lum[idx + 320] += err * (5 / 16);
              if (x + 1 < 320) lum[idx + 320 + 1] += err * (1 / 16);
            }
          }
        }

        for (let i = 0, p = 0; i < data.length; i += 4, p++) {
          const v = lum[p] > 127 ? 255 : 0;
          data[i] = v;
          data[i + 1] = v;
          data[i + 2] = v;
        }
      } else {
        for (let i = 0; i < data.length; i += 4) {
          const gray = Math.round(0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2]);
          let on = gray < th;
          if (inv) on = !on;
          const v = on ? 255 : 0;
          data[i] = v;
          data[i + 1] = v;
          data[i + 2] = v;
        }
      }

      ctx.putImageData(img, 0, 0);
      uploadBtn.disabled = false;
    }

    function buildPackedBytes() {
      const img = ctx.getImageData(0, 0, 320, 120);
      const data = img.data;
      const out = new Uint8Array(0x12c1);
      let outIndex = 0;

      for (let y = 0; y < 120; y++) {
        for (let x = 0; x < 320; x += 8) {
          let b = 0;
          for (let bit = 0; bit < 8; bit++) {
            const px = x + bit;
            const i = (y * 320 + px) * 4;
            const isInk = data[i] === 255;
            if (isInk) b |= (1 << bit);
          }
          out[outIndex++] = b;
        }
      }

      out[out.length - 1] = 0;
      return out;
    }

    fileInput.addEventListener('change', () => {
      const f = fileInput.files && fileInput.files[0];
      if (!f) {
        loadedImage = null;
        renderPreview();
        return;
      }
      const img = new Image();
      img.onload = () => {
        loadedImage = img;
        renderPreview();
        setStatus('preview ready');
      };
      img.onerror = () => setStatus('failed to load image');
      img.src = URL.createObjectURL(f);
    });

    threshold.addEventListener('input', () => {
      thv.textContent = threshold.value;
      renderPreview();
    });

    invert.addEventListener('change', renderPreview);
    dither.addEventListener('change', renderPreview);

    uploadBtn.addEventListener('click', async () => {
      try {
        const bytes = buildPackedBytes();
        const form = new FormData();
        form.append('image', new Blob([bytes], { type: 'application/octet-stream' }), 'image.bin');
        setStatus('uploading...');
        const r = await fetch('/api/upload', { method: 'POST', body: form });
        const t = await r.text();
        if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
        setStatus(t || 'uploaded');
      } catch (e) {
        setStatus('upload failed: ' + e.message);
      }
    });

    clearBtn.addEventListener('click', async () => {
      try {
        const r = await fetch('/api/clear', { method: 'POST' });
        const t = await r.text();
        if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
        setStatus(t || 'cleared');
      } catch (e) {
        setStatus('clear failed: ' + e.message);
      }
    });

    async function loadTuning() {
      try {
        const r = await fetch('/api/tuning');
        if (!r.ok) throw new Error('HTTP ' + r.status);
        const cfg = await r.json();
        reportIntervalMs.value = cfg.reportIntervalMs;
        stopPressHoldMs.value = cfg.stopPressHoldMs;
        moveXHoldMs.value = cfg.moveXHoldMs;
        anchorMoveHoldMs.value = cfg.anchorMoveHoldMs;
        moveYHoldMs.value = cfg.moveYHoldMs;
      } catch (e) {
        setStatus('load tuning failed: ' + e.message);
      }
    }

    saveTuningBtn.addEventListener('click', async () => {
      try {
        const body = new URLSearchParams({
          reportIntervalMs: reportIntervalMs.value,
          stopPressHoldMs: stopPressHoldMs.value,
          moveXHoldMs: moveXHoldMs.value,
          anchorMoveHoldMs: anchorMoveHoldMs.value,
          moveYHoldMs: moveYHoldMs.value,
        });

        const r = await fetch('/api/tuning', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body,
        });
        const t = await r.text();
        if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
        setStatus(t || 'tuning saved');
      } catch (e) {
        setStatus('save tuning failed: ' + e.message);
      }
    });

    doneBtn.addEventListener('click', async () => {
      try {
        const now = Date.now();
        if (now > doneArmedUntil) {
          doneArmedUntil = now + 5000;
          setStatus('5秒以内にもう一度押すと再起動します');
          return;
        }

        doneBtn.disabled = true;
        setStatus('restarting device...');
        const r = await fetch('/api/done', { method: 'POST' });
        if (!r.ok) throw new Error('HTTP ' + r.status);
      } catch (e) {
        doneBtn.disabled = false;
        doneArmedUntil = 0;
        setStatus('restart failed: ' + e.message);
      }
    });

    async function loadCurrentImage() {
      try {
        const r = await fetch('/api/image');
        if (!r.ok) return;
        const buf = await r.arrayBuffer();
        const src = new Uint8Array(buf);
        const imgData = ctx.createImageData(320, 120);
        const pixels = imgData.data;
        let byteIdx = 0;
        for (let y = 0; y < 120; y++) {
          for (let x = 0; x < 320; x += 8) {
            const b = src[byteIdx++];
            for (let bit = 0; bit < 8; bit++) {
              const i = (y * 320 + x + bit) * 4;
              const v = (b >> bit) & 1 ? 255 : 0;
              pixels[i] = v; pixels[i+1] = v; pixels[i+2] = v; pixels[i+3] = 255;
            }
          }
        }
        ctx.putImageData(imgData, 0, 0);
      } catch (e) {
        // 取得失敗時は無視
      }
    }

    loadTuning();
    loadCurrentImage();
  </script>
</body>
</html>
)HTML";

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

static bool load_custom_image_from_fs(void) {
  if (!LittleFS.exists(kCustomImagePath)) {
    active_image_data = image_data;
    custom_image_loaded = false;
    return false;
  }

  File f = LittleFS.open(kCustomImagePath, "r");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open custom image: %s", kCustomImagePath);
    active_image_data = image_data;
    custom_image_loaded = false;
    return false;
  }

  if ((int)f.size() != kImageDataSize) {
    ESP_LOGE(TAG, "Invalid custom image size: %d", (int)f.size());
    f.close();
    LittleFS.remove(kCustomImagePath);
    active_image_data = image_data;
    custom_image_loaded = false;
    return false;
  }

  const size_t read_len = f.read(custom_image_data, kImageDataSize);
  f.close();
  if (read_len != kImageDataSize) {
    ESP_LOGE(TAG, "Failed to read custom image bytes: %d", (int)read_len);
    active_image_data = image_data;
    custom_image_loaded = false;
    return false;
  }

  active_image_data = custom_image_data;
  custom_image_loaded = true;
  ESP_LOGI(TAG, "Custom image loaded from LittleFS");
  return true;
}

static void handle_web_root(void) {
  web_server.send_P(200, "text/html; charset=utf-8", kWebUiHtml);
}

static void handle_web_captive_redirect(void) {
  web_server.sendHeader("Location", "http://192.168.4.1/", true);
  web_server.send(302, "text/plain", "Redirecting to captive portal");
}

static void handle_web_status(void) {
  char json[128];
  const IPAddress ip = WiFi.softAPIP();
  snprintf(json, sizeof(json),
           "{\"customImage\":%s,\"ip\":\"%u.%u.%u.%u\"}",
           custom_image_loaded ? "true" : "false",
           ip[0], ip[1], ip[2], ip[3]);
  web_server.send(200, "application/json", json);
}

static int arg_to_int_or_default(const char *name, int default_value) {
  if (!web_server.hasArg(name)) {
    return default_value;
  }
  return web_server.arg(name).toInt();
}

static void handle_web_get_tuning(void) {
  char json[256];
  snprintf(json, sizeof(json),
           "{\"reportIntervalMs\":%d,\"stopPressHoldMs\":%d,\"moveXHoldMs\":%d,"
           "\"anchorMoveHoldMs\":%d,\"moveYHoldMs\":%d}",
           runtime_tuning.report_interval_ms,
           runtime_tuning.stop_press_hold_ms,
           runtime_tuning.move_x_hold_ms,
           runtime_tuning.anchor_move_hold_ms,
           runtime_tuning.move_y_hold_ms);
  web_server.send(200, "application/json", json);
}

// Forward declaration
static void save_tuning_to_fs(void);

static void handle_web_set_tuning(void) {
  runtime_tuning.report_interval_ms = clamp_int(
      arg_to_int_or_default("reportIntervalMs", runtime_tuning.report_interval_ms), 1, 33);
  runtime_tuning.stop_press_hold_ms = clamp_int(
      arg_to_int_or_default("stopPressHoldMs", runtime_tuning.stop_press_hold_ms), 1, 200);
  runtime_tuning.move_x_hold_ms = clamp_int(
      arg_to_int_or_default("moveXHoldMs", runtime_tuning.move_x_hold_ms), 1, 200);
  runtime_tuning.anchor_move_hold_ms = clamp_int(
      arg_to_int_or_default("anchorMoveHoldMs", runtime_tuning.anchor_move_hold_ms), 1, 200);
  runtime_tuning.move_y_hold_ms = clamp_int(
      arg_to_int_or_default("moveYHoldMs", runtime_tuning.move_y_hold_ms), 1, 200);

  save_tuning_to_fs();
  web_server.send(200, "text/plain", "tuning updated and persisted");
}

static void handle_web_upload_chunk(void) {
  HTTPUpload &upload = web_server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    upload_failed = false;
    upload_error_message = "";
    upload_received_bytes = 0;
    if (LittleFS.exists(kUploadTempPath)) {
      LittleFS.remove(kUploadTempPath);
    }
    upload_file = LittleFS.open(kUploadTempPath, "w");
    if (!upload_file) {
      upload_failed = true;
      upload_error_message = "failed to open temp file";
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (upload_failed) {
      return;
    }
    if (!upload_file) {
      upload_failed = true;
      upload_error_message = "temp file is not open";
      return;
    }
    const size_t written = upload_file.write(upload.buf, upload.currentSize);
    upload_received_bytes += written;
    if (written != upload.currentSize) {
      upload_failed = true;
      upload_error_message = "write failed";
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (upload_file) {
      upload_file.close();
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (upload_file) {
      upload_file.close();
    }
    upload_failed = true;
    upload_error_message = "upload aborted";
    LittleFS.remove(kUploadTempPath);
  }
}

static void handle_web_upload_finish(void) {
  if (upload_file) {
    upload_file.close();
  }

  if (upload_failed) {
    LittleFS.remove(kUploadTempPath);
    web_server.send(400, "text/plain", upload_error_message);
    return;
  }

  if ((int)upload_received_bytes != kImageDataSize) {
    LittleFS.remove(kUploadTempPath);
    web_server.send(400, "text/plain", "invalid data size (expect 0x12c1 bytes)");
    return;
  }

  if (LittleFS.exists(kCustomImagePath)) {
    LittleFS.remove(kCustomImagePath);
  }
  if (!LittleFS.rename(kUploadTempPath, kCustomImagePath)) {
    LittleFS.remove(kUploadTempPath);
    web_server.send(500, "text/plain", "failed to store image");
    return;
  }

  if (!load_custom_image_from_fs()) {
    web_server.send(500, "text/plain", "failed to apply uploaded image");
    return;
  }

  force_display_refresh = true;
  web_server.send(200, "text/plain", "uploaded and applied");
}

static void handle_web_clear(void) {
  if (LittleFS.exists(kCustomImagePath)) {
    LittleFS.remove(kCustomImagePath);
  }
  active_image_data = image_data;
  custom_image_loaded = false;
  force_display_refresh = true;
  web_server.send(200, "text/plain", "switched to built-in image");
}

static void handle_web_get_image(void) {
  // ESP32ではPROGMEMもRAMもアドレス空間が共通なのでsend_Pでどちらも送信できる。
  web_server.send_P(200, "application/octet-stream",
                    reinterpret_cast<PGM_P>(active_image_data), kImageDataSize);
}

static void load_tuning_from_fs(void) {
  if (!LittleFS.exists("/tuning.json")) {
    return;
  }
  File f = LittleFS.open("/tuning.json", "r");
  if (!f) return;
  
  String content = f.readString();
  f.close();
  
  // 簡易JSONパース: キーの値を抽出
  int val;
  if (sscanf(content.c_str(), "{\"reportIntervalMs\":%d", &val) == 1) {
    runtime_tuning.report_interval_ms = val;
  }
  if (strstr(content.c_str(), "\"stopPressHoldMs\":") != NULL) {
    sscanf(strstr(content.c_str(), "\"stopPressHoldMs\":"), "\"stopPressHoldMs\":%d", &val);
    runtime_tuning.stop_press_hold_ms = val;
  }
  if (strstr(content.c_str(), "\"moveXHoldMs\":") != NULL) {
    sscanf(strstr(content.c_str(), "\"moveXHoldMs\":"), "\"moveXHoldMs\":%d", &val);
    runtime_tuning.move_x_hold_ms = val;
  }
  if (strstr(content.c_str(), "\"moveYHoldMs\":") != NULL) {
    sscanf(strstr(content.c_str(), "\"moveYHoldMs\":"), "\"moveYHoldMs\":%d", &val);
    runtime_tuning.move_y_hold_ms = val;
  }
  if (strstr(content.c_str(), "\"anchorMoveHoldMs\":") != NULL) {
    sscanf(strstr(content.c_str(), "\"anchorMoveHoldMs\":"), "\"anchorMoveHoldMs\":%d", &val);
    runtime_tuning.anchor_move_hold_ms = val;
  }
}

static void save_tuning_to_fs(void) {
  File f = LittleFS.open("/tuning.json", "w");
  if (!f) return;
  
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"reportIntervalMs\":%d,\"stopPressHoldMs\":%d,\"moveXHoldMs\":%d,\"moveYHoldMs\":%d,\"anchorMoveHoldMs\":%d}",
    runtime_tuning.report_interval_ms,
    runtime_tuning.stop_press_hold_ms,
    runtime_tuning.move_x_hold_ms,
    runtime_tuning.move_y_hold_ms,
    runtime_tuning.anchor_move_hold_ms);
  f.write((uint8_t*)buf, strlen(buf));
  f.close();
}

static void handle_web_done(void) {
  web_server.send(200, "text/plain", "done");
  delay(100);
  esp_restart();
}

static void begin_web_ui(void) {
  WiFi.mode(WIFI_AP);
  const bool ap_ok = WiFi.softAP(kApSsid, kApPassword);
  if (!ap_ok) {
    ESP_LOGE(TAG, "Failed to start SoftAP");
  }

  const IPAddress ip = WiFi.softAPIP();
  ESP_LOGI(TAG, "Web UI AP: SSID=%s PASS=%s IP=%u.%u.%u.%u",
           kApSsid, kApPassword, ip[0], ip[1], ip[2], ip[3]);

  web_server.on("/", HTTP_GET, handle_web_root);
  // OSのキャプティブポータル判定URLを拾ってWeb UIへ誘導する。
  web_server.on("/generate_204", HTTP_GET, handle_web_captive_redirect);         // Android
  web_server.on("/gen_204", HTTP_GET, handle_web_captive_redirect);              // Android variants
  web_server.on("/hotspot-detect.html", HTTP_GET, handle_web_captive_redirect);  // iOS/macOS
  web_server.on("/library/test/success.html", HTTP_GET, handle_web_captive_redirect);  // iOS/macOS
  web_server.on("/ncsi.txt", HTTP_GET, handle_web_captive_redirect);             // Windows
  web_server.on("/connecttest.txt", HTTP_GET, handle_web_captive_redirect);      // Windows
  web_server.on("/api/status", HTTP_GET, handle_web_status);
  web_server.on("/api/tuning", HTTP_GET, handle_web_get_tuning);
  web_server.on("/api/tuning", HTTP_POST, handle_web_set_tuning);
  web_server.on("/api/upload", HTTP_POST, handle_web_upload_finish, handle_web_upload_chunk);
  web_server.on("/api/clear", HTTP_POST, handle_web_clear);
  web_server.on("/api/image", HTTP_GET, handle_web_get_image);
  web_server.on("/api/done", HTTP_POST, handle_web_done);
  web_server.onNotFound(handle_web_captive_redirect);
  web_server.begin();

  dns_server.start(53, "*", ip);
  ESP_LOGI(TAG, "Web UI started");
}

static bool current_pixel_should_ink(void) {
  const size_t index = image_index(printer_state.xpos, printer_state.ypos);
  const uint8_t bit = (uint8_t)(1U << (printer_state.xpos % 8));
  return (active_image_data[index] & bit) != 0;
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
  printer_state.row_settle_frames = tuning_row_anchor_settle_frames();
  printer_state.state = STATE_REANCHOR_ROW;
}

static bool image_pixel_should_ink(int x, int y) {
  if (x < 0 || x >= kImageWidth || y < 0 || y >= kImageHeight) {
    return false;
  }
  const size_t index = image_index(x, y);
  const uint8_t bit = (uint8_t)(1U << (x % 8));
  return (active_image_data[index] & bit) != 0;
}

static void build_web_ui_url(char *out, size_t out_size) {
  const IPAddress ip = WiFi.softAPIP();
  snprintf(out, out_size, "http://%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static void build_wifi_ap_qr_payload(char *out, size_t out_size) {
  // Wi-Fi設定QRフォーマット: WIFI:T:<auth>;S:<ssid>;P:<password>;;
  snprintf(out, out_size, "WIFI:T:WPA;S:%s;P:%s;;", kApSsid, kApPassword);
}

static void draw_plate_preview(int x, int y, int w, int h) {
  M5.Display.drawRect(x - 1, y - 1, w + 2, h + 2, TFT_DARKGREY);
  M5.Display.fillRect(x, y, w, h, TFT_BLACK);

  // 単純な最近傍縮小だと細線が欠けやすいので、対応元領域の黒率で描画する。
  constexpr int kInkPercentThreshold = 35;

  for (int dy = 0; dy < h; ++dy) {
    int sy0 = (dy * kImageHeight) / h;
    int sy1 = ((dy + 1) * kImageHeight) / h;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    if (sy1 > kImageHeight) sy1 = kImageHeight;

    for (int dx = 0; dx < w; ++dx) {
      int sx0 = (dx * kImageWidth) / w;
      int sx1 = ((dx + 1) * kImageWidth) / w;
      if (sx1 <= sx0) sx1 = sx0 + 1;
      if (sx1 > kImageWidth) sx1 = kImageWidth;

      int ink_count = 0;
      const int total_count = (sx1 - sx0) * (sy1 - sy0);
      for (int sy = sy0; sy < sy1; ++sy) {
        for (int sx = sx0; sx < sx1; ++sx) {
          if (image_pixel_should_ink(sx, sy)) {
            ++ink_count;
          }
        }
      }

      if ((ink_count * 100) >= (total_count * kInkPercentThreshold)) {
        M5.Display.drawPixel(x + dx, y + dy, TFT_WHITE);
      }
    }
  }
}

static display_state_t get_display_state(void) {
  if (show_web_qr) {
    return DISPLAY_WEB_QR;
  }
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
  M5.Display.println("");
  M5.Display.println("Hold BtnA: QR");
  M5.Display.println("(2 seconds)");
}

static void draw_ready_screen(int preview_w) {
  M5.Display.setCursor(0, 14);
  M5.Display.setTextColor(custom_image_loaded ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  M5.Display.println(custom_image_loaded ? "Image: custom" : "Image: built-in");
  M5.Display.setCursor(0, 20);
  draw_plate_preview(kPreviewX, kPreviewY, preview_w, kPreviewHeight);
  M5.Display.setCursor(0, 72);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.println("Ready");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("");
  M5.Display.println("Press BtnA");
  M5.Display.println("to start");
  M5.Display.println("Hold BtnA: QR");
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
  M5.Display.println("Hold BtnA: QR");
}

static void draw_web_qr_screen(void) {
  char wifi_qr[96];
  char url[48];
  build_wifi_ap_qr_payload(wifi_qr, sizeof(wifi_qr));
  build_web_ui_url(url, sizeof(url));

  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.println("AP Join QR");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.println("Hold BtnA to close");

  M5.Display.qrcode(wifi_qr, 20, 20, 88, 5);

  M5.Display.setCursor(0, 112);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.printf("SSID:%s\n", kApSsid);
  M5.Display.printf("Open:%s\n", url);
}

static void render_display(bool force) {
  // BtnA 長押し中のフィードバック: 画面点滅
  if (btn_a_being_held_for_qr) {
    // 500ms周期で点滅（250ms ON, 250ms OFF）
    if ((millis() / 250) % 2 == 1) {
      // 画面OFF時は黒く塗り潰すだけで終了
      M5.Display.startWrite();
      M5.Display.fillScreen(TFT_BLACK);
      M5.Display.endWrite();
      return;
    }
  }

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

    case DISPLAY_WEB_QR:
      draw_web_qr_screen();
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
      printer_state.echoes = tuning_stop_press_echoes();  // A押下をエコーで確実に届ける
      break;

    case STATE_STOP_Y:
      press_a_if_current_pixel_should_ink(report);
      if (printer_state.ypos < (kImageHeight - 1)) {
        printer_state.pre_move_y_settle_frames = tuning_pre_move_y_settle_frames();
        printer_state.state = STATE_PRE_MOVE_Y_SETTLE;
      } else {
        printer_state.state = STATE_DONE;
      }
      printer_state.echoes = tuning_stop_press_echoes();  // A押下をエコーで確実に届ける
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
      printer_state.echoes = tuning_move_x_echoes();
      printer_state.post_move_x_settle_frames = tuning_post_move_x_settle_frames();
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
      printer_state.echoes = tuning_move_y_echoes();
      printer_state.post_move_y_settle_frames = tuning_post_move_y_settle_frames();
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
      printer_state.echoes = tuning_anchor_move_echoes();
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
    dns_server.processNextRequest();
    web_server.handleClient();
    M5.update();

    if (M5.BtnA.wasPressed()) {
      btn_a_press_start_ms = millis();
      btn_a_long_press_handled = false;
      btn_a_being_held_for_qr = false;
    }

    if (M5.BtnA.isPressed() && !btn_a_long_press_handled) {
      const uint32_t pressed_ms = millis() - btn_a_press_start_ms;
      if (pressed_ms >= (uint32_t)kBtnALongPressMs) {
        btn_a_long_press_handled = true;
        btn_a_being_held_for_qr = false;
        if (!show_web_qr) {
          show_web_qr = true;
          begin_web_ui();
        }
        start_requested = false;
        force_display_refresh = true;
      } else if (pressed_ms > 500) {
        // 長押し判定前の途中で、ユーザーへのフィードバック開始
        btn_a_being_held_for_qr = true;
      }
    }

    if (M5.BtnA.wasReleased()) {
      btn_a_being_held_for_qr = false;
      if (!btn_a_long_press_handled) {
        if (printer_state.armed) {
          // 印刷中の短押しは即中止して、入力を中立に戻す。
          printer_state.armed = false;
          start_requested = false;
          if (switch_hid.ready()) {
            switch_input_report_t neutral_report;
            reset_report(&neutral_report);
            switch_hid.SendReport(0, &neutral_report, sizeof(neutral_report));
          }
          force_display_refresh = true;
        } else if (!show_web_qr) {
          start_requested = true;
        }
      }
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

    vTaskDelay(pdMS_TO_TICKS(tuning_interval_ms()));
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

  // LittleFS 初期化（web_ui は QRモード突入時のみ）
  if (!LittleFS.begin(true)) {
    ESP_LOGE(TAG, "LittleFS mount failed");
  } else {
    load_tuning_from_fs();
  }

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