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

2. **BtnA（正面の丸ボタン）** を短押しすると描画が開始される

3. LCD に描画進捗（座標・パーセンテージ・プログレスバー）が表示される

4. `Done / Press BtnA to run again` が表示されたら完了  
   もう一度 BtnA を短押しすると最初から再描画できる

### 描画画像の変更（Web UI）

Web UI から 320×120px 相当の画像をアップロードして描画内容を変更できる。

1. 通常画面で **BtnA を 1.2 秒長押し** すると QR モードへ入る
2. LCD に Wi-Fi 接続用 QR（SSID: `AtomS3-ImageUI` / PW: `12345678`）が表示される
3. スマホ等で QR を読み取って AP に接続する
4. 接続が検知されると 500ms 後に **URL QR** へ自動切替する
5. URL QR を読み取るかブラウザで `192.168.4.1` を開いて Web UI を操作する
6. **BtnA 短押し** で QR モードを終了し通常画面へ戻る（AP も停止する）

---

## ビルド・書き込み

```bash
# ビルド
PATH=/Users/katano/.platformio/penv/bin:$PATH pio run -e atoms3_arduino

# 書き込み（VS Code の Upload タスクでも可）
PATH=/Users/katano/.platformio/penv/bin:$PATH pio run -e atoms3_arduino -t upload
```

---

## 画像の準備（静的データとして焼く場合）

Web UI を使わず、ファームウェアに画像を直接焼く場合。

1. `plate.png` を 320×120px で用意する
2. 黒ピクセル → インク、白ピクセル → 無し（デフォルト）
3. 変換スクリプトを実行する

```bash
python3 png2c.py -t esp32 -o src/image_data.c plate.png
```

### データ検証スクリプト

`debug_image.py` を実行すると `image_data.c` を画像に復元して差分チェックできる。

```bash
python3 debug_image.py
# → debug_reconstructed.png 等を出力
```

---

## ファイル構成

```
src/
  main.cpp           メインロジック（HID・Web UI・表示タスク）
  image_data.c       組み込み画像データ（320×120 1bit packed）
  image_data.h
images/
  image_data-mao.c           バックアップ用画像（マオ）
  image_data-karasu-tonbi.c  バックアップ用画像（カラストンビ）
png2c.py             PNG → image_data.c 変換スクリプト
```

`images/` 内のファイルはビルド対象外。差し替える場合は `src/image_data.c` へコピーする。

---

## ボタン操作仕様

| 画面状態 | BtnA 短押し | BtnA 長押し（1.2秒） |
|---|---|---|
| 通常待機（Ready/Done） | 印刷開始 | QRモードへ |
| 印刷中 | 即中止（中立レポート送信） | なし |
| QRモード | 通常画面へ戻る（AP停止） | なし |

---

## QRモード・APモード仕様

- AP は **QRモード突入時にのみ起動** し、QRモード終了時に停止する
- QR 表示の流れ:
  1. Wi-Fi 接続用 QR（AP 参加用）を表示
  2. 端末の AP 接続を検知 → 500ms デバウンス → URL QR へ自動切替
  3. BtnA 短押しで QRモード終了・AP停止・通常画面へ
- キャプティブポータル: DNS キャッチオール + 主要 OS 検出 URL リダイレクト対応

---

## Web UI 機能

| 機能 | 説明 |
|---|---|
| 画像アップロード | PNG/JPEG/WEBP/BMP をブラウザ側で 320×120 1bit 化して送信 |
| プレビュー | canvas でリアルタイム 1bit 変換結果を表示（threshold / invert 調整可） |
| 現在画像の表示 | ページ読み込み時に `/api/image` からデバイス上の画像を取得して canvas に描画 |
| built-in 画像へ戻す | `Use built-in image` ボタン |
| ランタイムチューニング | 送信間隔・各保持時間(ms)を Web UI から変更（再起動まで有効） |

### API エンドポイント

| エンドポイント | メソッド | 説明 |
|---|---|---|
| `/` | GET | Web UI HTML |
| `/api/status` | GET | ステータス JSON |
| `/api/image` | GET | 現在の画像データ（320×120 1bit packed、4801バイト） |
| `/api/upload` | POST | 画像バイナリアップロード（LittleFS `/image.bin` に保存） |
| `/api/clear` | POST | 組み込み画像に戻す（`/image.bin` を削除） |
| `/api/tuning` | GET | チューニング値取得 JSON |
| `/api/tuning` | POST | チューニング値設定（フォームデータ） |

### 画像データ形式（`/image.bin`）

- 320×120 ピクセル、1bit packed
- 合計 4801 バイト（`0x12c1`）
- バイト内のビット順: LSB → 左ピクセル、MSB → 右ピクセル
- 行順: 上から順（Y=0 が先頭）
- 起動時に LittleFS から自動読み込み。ファイルがなければ組み込み画像を使用

---

## 動作フロー（印刷）

1. 起動 → LittleFS から `/image.bin` を読み込み（なければ組み込み画像を使用）
2. LCD に "Ready / Press BtnA" を表示
3. BtnA 短押し → コントローラー同期 → 原点合わせ → 描画開始
4. 蛇行走査（偶数行: 左→右、奇数行: 右→左）で全 320×120 ピクセルを描画
5. 完了 → LCD に "Done / Press BtnA to run again"

---

## チューニングパラメータ（`src/main.cpp` の `cursor_tuning` namespace）

すべて ms 単位で直接指定する。フレーム数は `kReportIntervalMs` で自動計算される。

| 定数名 | 現在値 | 説明 |
|---|---|---|
| `kReportIntervalMs` | 5 ms | HID レポート送信ループ周期 |
| `kRowAnchorOvershootSteps` | 0 | 行開始時の端方向オーバーシュート量（0=無効） |
| `kRowAnchorSettleMs` | 0 ms | アンカー後の中立待機時間 |
| `kStopPressHoldMs` | 15 ms | A ボタン押下の保持時間 |
| `kMoveXHoldMs` | 15 ms | X 方向スティック入力の保持時間 |
| `kAnchorMoveHoldMs` | 15 ms | アンカー移動時のスティック保持時間 |
| `kMoveYHoldMs` | 10 ms | Y 方向スティック入力の保持時間 |
| `kPreMoveYSettleMs` | 10 ms | Y 移動前の中立待機時間 |
| `kPostMoveYSettleMs` | 10 ms | Y 移動後の中立待機時間 |
| `kPostMoveXSettleMs` | 0 ms | X 移動後の中立待機時間 |

Web UI の Tuning セクションから `intervalMs` / `stopPressHoldMs` / `moveXHoldMs` / `moveYHoldMs` / `anchorMoveHoldMs` をランタイムで上書きできる（再起動で初期値に戻る）。

---

## 所要時間の目安

| 設定 | 所要時間 |
|---|---|
| 初期（5ms / echoes=2 一律） | 約3分（高速だがズレやすい） |
| 精度優先版（16ms / echoes=4 一律） | 約15〜20分（遅いが安定） |
| 現在（5ms / STOP=15ms / MOVE_X=15ms / MOVE_Y=10ms） | 約5〜8分 |

---

## 座標ズレ修正ログ

### [1] 移動中の A 押下を禁止
- **問題**: `STATE_MOVE_X/Y` 中にも印字判定が走り、移動フレームで誤って A が押されていた
- **修正**: A 押下判定を `STATE_STOP_X/Y`（座標確定フレーム）のみに限定

### [2] 左右端アンカー（過走査）の追加
- **問題**: 行ごとに座標ズレが累積し、右端に到達できないことがあった
- **修正**: 各行開始時に端方向へ押し続けて壁当て → 安定待ち → 描画開始

### [3] SendReport 失敗時の巻き戻し
- **問題**: `build_next_report` で状態更新後に `SendReport` 失敗 → 内部座標だけ進む
- **修正**: 送信前に `printer_state` を保存し、失敗時に復元

### [4] 表示タスクの分離
- **問題**: LCD 描画ループが HID タスクをブロックし送信周期が乱れていた
- **修正**: `display_task`（優先度3）を分離し、HID タスク（優先度5）は送信のみ専念

### [5] チューニングパラメータの ms 直接指定化
- **修正**: フレーム数でなく ms で直接指定する `cursor_tuning` namespace に集約。フレーム数は自動計算

### [6] 移動系・A 押下系で保持時間を分離
- **問題**: 統一エコー数だと Y 方向に過移動が発生
- **修正**: STOP 系・MOVE_X 系・MOVE_Y 系で保持時間を個別設定

### [7] X 移動の保持時間を個別調整
- **問題**: Y 方向単発化後、X 方向の取りこぼしが発生するケースが出た
- **修正**: X 方向と左右アンカーのみ短い保持時間を付与

### [8] Y 移動の 0px 取りこぼし対策
- **問題**: Y 移動完全単発化でまれに 0px になるケースがあった
- **修正**: Y 方向も最低限の保持時間（10ms）を確保

---

## 参考にしたリポジトリ

- https://github.com/shinyquagsire23/Switch-Fightstick
- https://github.com/Loloweb/Switch-Fightstick
- https://github.com/progmem/Switch-Fightstick
