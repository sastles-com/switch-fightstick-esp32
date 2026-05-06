# Switch Fightstick — Splatoon プレート印刷ツール

M5Stack AtomS3 を Nintendo Switch の USB HID コントローラーとして認識させ、
スプラトゥーンのなまえプレートに任意の画像を自動描画するツール。

---

## ハードウェア

| 項目 | 内容 |
|---|---|
| ボード | M5Stack AtomS3 (ESP32-S3) |
| 接続 | USB-C → Nintendo Switch |
| VID/PID | 0x0F0D / 0x0092 (HORI POKKEN CONTROLLER) |

---

## 使用方法

### 事前準備

1. スプラトゥーンのなまえプレート編集画面を開く  
   `ロビー → 自分のアイコン → なまえプレート → へんしゅう`

2. 「もよう」を選択し、**何も描かれていない白紙の状態**にする  
   （既存の絵が残っていると位置がずれる原因になる）

3. 描くツールを **一番小さいサイズのブラシ** に設定する

4. カーソルを **左上隅** に合わせる  
   （プログラムはここを原点として動作する）

5. M5Stack AtomS3 を USB-C ケーブルで Switch に接続する

### 描画の開始

1. AtomS3 を接続すると LCD に `Ready / Press BtnA` が表示される  
   （`USB not mounted` が続く場合はケーブルや接続先を確認）

2. **BtnA（正面の丸ボタン）** を押すと描画が開始される

3. LCD に描画進捗（座標・パーセンテージ・プログレスバー）が表示される

4. `Done / Press BtnA to run again` が表示されたら完了  
   もう一度 BtnA を押すと最初から再描画できる

### 注意事項

- 描画中は Switch のコントローラー入力を触らない
- 描画中に USB を抜くと最初からやり直しになる
- 描画時間は設定によって異なる（精度優先設定で約15〜20分）
- カーソル位置がズレた場合は `kRowAnchorOvershootSteps` を増やして再試行する

---

## ビルド・書き込み

```bash
# ビルド
PATH=/Users/katano/.platformio/penv/bin:/opt/homebrew/bin:/usr/bin:/bin \
  /Users/katano/.platformio/penv/bin/pio run -e atoms3_arduino

# 書き込み
PATH=/Users/katano/.platformio/penv/bin:/opt/homebrew/bin:/usr/bin:/bin \
  /Users/katano/.platformio/penv/bin/pio run -e atoms3_arduino -t upload \
  --upload-port /dev/cu.usbmodem1101
```

VS Code の Upload タスク（`platformio run --target upload`）でも書き込み可能。

---

## 画像の準備

1. `plate.png` を 320×120px で用意する
2. 黒ピクセル → インク、白ピクセル → 無し（デフォルト）
3. 変換スクリプトを実行する

```bash
python3 png2c.py -t esp32 -o src/image_data.c plate.png
```

### データ検証スクリプト

`debug_image.py` を実行すると `image_data.c` を画像に復元して `plate.png` と差分チェックし、
デバッグ用分割画像（4x拡大）を出力する。

```bash
python3 debug_image.py
# → debug_reconstructed.png, debug_top_half.png, debug_bottom_half.png など
```

---

## 動作フロー

1. 起動 → LCD に "Ready / Press BtnA" を表示
2. BtnA 押下 → コントローラー同期 → 原点合わせ → 描画開始
3. 蛇行走査（偶数行: 左→右、奇数行: 右→左）で全 320×120 ピクセルを描画
4. 完了 → LCD に "Done / Press BtnA to run again"

---

## 修正ログ

### 座標ズレ対策

#### [1] 移動中のA押下を禁止
- **問題**: `STATE_MOVE_X` / `STATE_MOVE_Y` 中にも印字判定が走り、移動フレームで誤って A が押されていた
- **修正**: A 押下判定を `STATE_STOP_X` / `STATE_STOP_Y`（座標確定フレーム）のみに限定
- **ファイル**: `src/main.cpp` — `press_a_if_current_pixel_should_ink()` を STOP 状態のみで呼ぶ

#### [2] 左右端アンカー（過走査）の追加
- **問題**: 行ごとに座標ズレが累積し、右端に到達できないことがあった
- **修正**: 各行の開始時に端方向へ `kRowAnchorOvershootSteps` フレーム押し続けて壁に当て、
  その後 `kRowAnchorSettleFrames` フレーム入力なしで安定させてから描画開始する
- **追加状態**: `STATE_REANCHOR_ROW`（過走査）、`STATE_REANCHOR_SETTLE`（安定待ち）
- **ファイル**: `src/main.cpp` — `begin_row_anchor()`、`STATE_REANCHOR_ROW`、`STATE_REANCHOR_SETTLE`

#### [3] SendReport 失敗時の巻き戻し
- **問題**: `build_next_report` で状態を更新した後 `SendReport` が失敗すると、
  入力が Switch に届かないまま `xpos` だけ進んでズレていた
- **修正**: `build_next_report` 前に `printer_state` を保存し、`SendReport` が `false` を返したら復元する
- **ファイル**: `src/main.cpp` — `usb_report_task` 内の `saved_state` 巻き戻し処理

#### [4] 表示タスクの分離
- **問題**: LCD の描画（`draw_plate_preview` で 320×120 ループ）が HID タスクをブロックし、
  `vTaskDelay(5ms)` の周期が実際は 10〜20ms になって Switch 側に入力ロストが発生していた
- **修正**: `render_display` を優先度 3 の `display_task` へ切り出し、
  HID タスク（優先度 5）は USB 送信のみに専念させる
- **ファイル**: `src/main.cpp` — `display_task`（新規）、`usb_report_task` から描画処理を削除

#### [5] レポート送信間隔・精度パラメータの調整
- **問題**: 5ms 送信が Switch の USB HID ポーリング（約 8ms）より速く、バッファ上書きでロストしていた
- **修正**: 各パラメータを精度優先に調整

| パラメータ | 初期値 | 最終値 | 意図 |
|---|---|---|---|
| `kReportIntervalMs` | 5ms | 16ms | Switch polling の2倍マージン |
| `kRowAnchorOvershootSteps` | 6 | 30 | 端壁への押し付けを確実に |
| `kRowAnchorSettleFrames` | 2 | 5 | 安定待ちを長く |
| `echoes`（STOP系のみ適用） | 2 | 2 | STOP状態のA押下のみ繰り返し。移動系はエコーなし |

#### [6] 移動系・A押下系でエコー数を分離

- **問題**: `echoes=4` × `16ms` = 80ms ずっと `HAT_BOTTOM` / `HAT_RIGHT` / `HAT_LEFT` が押され続け、ゲーム側で5〜6行・列分カーソルが余分に移動していた（縦方向が2倍以上の間隔になる症状）
- **修正**: エコーを状態ごとに個別設定する構造に変更

| 状態 | エコー | 理由 |
|---|---|---|
| `STATE_STOP_X` / `STATE_STOP_Y` | 2フレーム | A ボタン押下を確実に届ける |
| `STATE_MOVE_X` / `STATE_MOVE_Y` | 0（なし） | 1フレームのみ入力して1ピクセル移動 |
| `STATE_REANCHOR_ROW` / `STATE_REANCHOR_SETTLE` | 0（なし） | 毎フレーム1ステップずつ制御する |

- **ファイル**: `src/main.cpp` — `STATE_STOP_X` / `STATE_STOP_Y` 内で `printer_state.echoes = 2` を個別設定

#### [7] X移動の保持時間を個別調整

- **問題**: Y方向の過移動対策で移動系を単発化した後、X方向だけ入力が通りにくくなるケースが出た
- **修正**: X方向移動と左右アンカーのみ短い保持（2フレーム）を付与し、Y方向は単発維持

| 状態 | エコー | 目的 |
|---|---|---|
| `STATE_MOVE_X` | 1（合計2フレーム） | X移動の取りこぼし対策 |
| `STATE_REANCHOR_ROW` | 1（合計2フレーム） | 左右端へのアンカー精度向上 |
| `STATE_MOVE_Y` | 0（単発） | 縦方向の過移動を抑制 |

- **ファイル**: `src/main.cpp` — `kMoveXEchoes` と `kAnchorMoveEchoes` を追加

---

## 現在のパラメータ設定（`src/main.cpp` の `namespace` ブロック）

| 定数名 | 現在値 | 単位 | 説明 | 小さくすると | 大きくすると |
|---|---|---|---|---|---|
| `kReportIntervalMs` | 5 | ms | HID レポートの送信間隔 | 速くなるがロストしやすい | 遅くなるが安定しやすい |
| `kRowAnchorOvershootSteps` | 0 | フレーム | 行開始時の端方向過走査フレーム数 | アンカー動作がなくなる | 端合わせが強くなるが遅くなる |
| `kRowAnchorSettleFrames` | 0 | フレーム | アンカー後の待機フレーム数 | 即描画開始 | 安定するが遅くなる |
| `kStopPressEchoes` | 2 | フレーム | STOP状態のA押下を繰り返す回数 | A押下取りこぼしが増える | A押下は安定するが遅くなる |
| `kMoveXEchoes` | 2 | フレーム | X移動入力の保持回数（合計3フレーム） | X移動が通りにくくなる | X移動は通りやすいが過移動リスク増 |
| `kAnchorMoveEchoes` | 2 | フレーム | アンカー移動入力の保持回数（合計3フレーム） | 端到達が不安定になる | 端到達は安定するが速度低下 |
| `kMoveYEchoes` | 0 | フレーム | Y移動入力の保持回数（合計1フレーム） | さらに単発化される | 縦過移動リスクが増える |
| `kPostMoveXSettleFrames` | 0 | フレーム | X移動後の中立待機フレーム数 | すぐ次入力へ | 安定するが遅くなる |
| `kPostMoveYSettleFrames` | 2 | フレーム | Y移動後の中立待機フレーム数 | 連続下入力が起きやすい | 縦方向の過移動を抑えやすい |
| `kDisplayRefreshMs` | 100 | ms | LCD表示の更新間隔 | 表示更新が細かくなる | 表示更新が粗くなる |

---

## パラメータ設定の履歴

| 項目 | 変更履歴 | 目的・理由 |
|---|---|---|
| `kReportIntervalMs` | 5 → 16 → 8 | 取りこぼし対策で一度16msへ。縦過移動抑制と速度バランスで8msへ再調整 |
| `kRowAnchorOvershootSteps` | 6 → 10 → 20 → 30 → 45 → 0 | 端合わせ強化を試行後、Original互換検証のため0へ |
| `kRowAnchorSettleFrames` | 2 → 5 → 0 | 安定待ち追加を試行後、Original互換検証のため0へ |
| STOP系エコー | 2 → 4 → 2 → 3 → 2 | 全体4を分離。最終的にOriginal互換2へ |
| X移動エコー | 0 → 1 → 2 | X入力不足対策として段階的に増加 |
| アンカー移動エコー | 0 → 1 → 3 → 2 | 端到達改善を試行後、Original互換寄りに調整 |
| Y移動エコー | 0 → 2 → 0 | Original互換検証後、縦過移動抑制のため0へ戻し |
| `kPostMoveYSettleFrames` | 0 → 2 | Y移動後の惰性入力抑制を追加 |

---

## デバッグ対応（実施済み）

1. image_data の逆変換検証
- `debug_image.py` で `src/image_data.c` を PNG に復元
- `plate.png` との差分ピクセルを計算して一致確認

2. 送信失敗時の状態巻き戻し
- `SendReport` 失敗時に `printer_state` を復元
- 内部座標だけ進む不整合を防止

3. 表示負荷とUSB送信の分離
- `display_task` と `usb_report_task` を分割
- LCD描画による HID 送信遅延を低減

4. X/Y 別の入力制御
- Xは保持時間を増やし、Yは単発+中立待機で過移動を抑制

---

## 参考にしたリポジトリ

- https://github.com/shinyquagsire23/Switch-Fightstick
- https://github.com/Loloweb/Switch-Fightstick
- https://github.com/progmem/Switch-Fightstick

---

## 所要時間の目安

状態ごとにエコー数が異なるため、以下は実運用時の目安時間。

| 設定 | 所要時間 |
|---|---|
| 初期（5ms / echoes=2 一律） | 約3分（高速だがズレやすい） |
| 精度優先版（16ms / echoes=4 一律） | 約15〜20分（遅いが安定） |
| 現在（5ms / STOP=2 / MOVE_X=2 / MOVE_Y=0 / postY=2） | 約5〜8分（X優先でY過移動を抑制） |
