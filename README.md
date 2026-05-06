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

---（`src/main.cpp` の `namespace` ブロック）

| 定数名 | 現在値 | 単位 | 説明 | 小さくすると | 大きくすると |
|---|---|---|---|---|---|
| `kReportIntervalMs` | 16 | ms | HID レポートの送信間隔 | 速くなるがロストしやすい | 遅くなるが確実 |
| `kRowAnchorOvershootSteps` | 30 | フレーム | 行開始時に端方向へ押し続けるフレーム数 | 端への押し付けが弱くなる | 確実に端に当たるが遅くなる |
| `kRowAnchorSettleFrames` | 5 | フレーム | 端到達後に入力なしで待つフレーム数 | カーソルが安定しないまま描画開始 | より安定するが遅くなる |
| `kDisplayRefreshMs` | 100 | ms | LCD 表示の更新間隔 | 表示が滑らかになる（HID への影響は分離済みで少ない） | 表示更新が粗くなる |

`echoes`（同一入力の繰り返し回数）は `build_next_report` 末尾の `printer_state.echoes = 4;` で変更。
大きくするほど 1 フレームのロストを補完しやすいが、1 ピクセル移動の確定に時間がかかる。

---

## 所要時間の目安

320列 × 120行 × (1 + echoes) フレーム × kReportIntervalMs で概算。

| 設定 | 所要時間 |
|---|---|
| 5ms / echoes=2 | 約3分（精度低）|
| 8ms / echoes=2 | 約5分 |
| 16ms / echoes=4 | 約15〜20分（精度優先）|
