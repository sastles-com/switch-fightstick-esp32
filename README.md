# Switch Fightstick - Splatoon Plate Printer

M5Stack AtomS3 を Nintendo Switch の USB HID コントローラーとして認識させ、
スプラトゥーンのなまえプレートに 320x120 の 1bit 画像を自動描画するツールです。

## 概要

- ボード: M5Stack AtomS3 (ESP32-S3)
- 接続: USB-C -> Nintendo Switch
- HID 識別: HORI POKKEN CONTROLLER 互換
- 画像入力方法:
  - ファームに組み込んだ `plate.png`
  - Web UI からアップロードしたカスタム画像

## クイックスタート

1. Switch でなまえプレート編集画面を開く
2. 白紙の状態にする
3. 一番小さいブラシを選ぶ
4. カーソルを左上に合わせる
5. AtomS3 を Switch に接続する
6. 画面に `Ready` が出たら BtnA を短押しする

## 操作方法

### 通常描画

1. AtomS3 を Switch に接続する
2. 画面に `Ready` が表示される
3. BtnA を短押しすると描画開始
4. 描画中は座標・進捗率・プログレスバーが表示される
5. `Done` 表示後に BtnA を短押しすると再実行できる

### BtnA 操作一覧

| 状態 | BtnA 短押し | BtnA 長押し |
|---|---|---|
| Ready / Done | 描画開始 / 再実行 | QR モードへ |
| 描画中 | 即中止 | なし |
| QR モード | 終了して通常画面へ戻る | なし |

長押し判定は 2 秒です。

## Web UI

### 開き方

1. 通常画面で BtnA を 2 秒長押しする
2. AtomS3 が AP を起動する
3. 画面の Wi-Fi QR を読み取り、`AtomS3-ImageUI` に接続する
4. ブラウザで `http://192.168.4.1` を開く

補足:

- OS によってはキャプティブポータルが自動表示されます
- キャプティブポータルのウィンドウサイズ自体は OS 側制御です
- `Open in browser` ボタンは、OS 制限で外部ブラウザ起動に失敗する場合があります

### Web UI でできること

- PNG / JPEG / WEBP / BMP のアップロード
- 320x120 / 1bit への変換プレビュー
- Threshold / Invert / Dither の調整
- Preview scale の変更
- Page height の変更
- Tuning 値の保存
- `Use built-in image` による組み込み画像への復帰

### 組み込み画像とカスタム画像の優先順位

起動時は以下の優先順位で画像を読み込みます。

1. LittleFS 上の `/image.bin` があればそれを使用
2. なければファームに組み込まれた画像を使用

そのため、`plate.png` を変更してファームを書き込み直しても、以前アップロードしたカスタム画像が残っていると見た目は変わりません。
組み込み画像を使いたい場合は、Web UI の `Use built-in image` を押して `/image.bin` を削除してください。

## 開発者向け: ビルドと書き込み

### ビルド

```bash
/Users/katano/Library/Python/3.9/bin/pio run -e atoms3_arduino
```

### 書き込み

```bash
/Users/katano/Library/Python/3.9/bin/pio run -e atoms3_arduino -t upload --upload-port /dev/cu.usbmodem1101
```

### 組み込み画像データの更新

`plate.png` を更新したあと、以下で `src/image_data.c` を再生成します。

```bash
python3 png2c.py -t esp32 -o src/image_data.c plate.png
```

その後にビルドし直してください。

### 画像データ確認

```bash
python3 debug_image.py
```

`debug_reconstructed.png` などが出力され、`src/image_data.c` の内容確認に使えます。

## 配布用ファイル

配布用の生成物は `dist/` に置きます。

- 配布概要: `dist/README-distribution.txt`
- macOS 手順: `dist/MAC-SETUP.md`
- Windows 手順: `dist/WINDOWS-SETUP.md`

主な配布物:

- `splatoon-atoms3-merged.bin`: 1本化イメージ
- `flash_merged.sh`: macOS / Linux 用自動書き込み
- `flash_merged.bat`: Windows cmd 用自動書き込み
- `flash_merged.ps1`: Windows PowerShell 用自動書き込み

## ファイル構成

```text
src/
  main.cpp         メインロジック（HID / Web UI / LCD 表示）
  image_data.c     組み込み画像データ
  image_data.h
dist/
  README-distribution.txt
  MAC-SETUP.md
  WINDOWS-SETUP.md
  flash_merged.sh
  flash_merged.bat
  flash_merged.ps1
png2c.py           plate.png -> src/image_data.c 変換スクリプト
plate.png          組み込みデフォルト画像
debug_image.py     image_data.c の復元確認スクリプト
```

## API エンドポイント

| エンドポイント | メソッド | 説明 |
|---|---|---|
| `/` | GET | Web UI |
| `/api/status` | GET | ステータス JSON |
| `/api/image` | GET | 現在有効な 1bit 画像データ |
| `/api/upload` | POST | カスタム画像アップロード |
| `/api/clear` | POST | カスタム画像削除 -> 組み込み画像に戻す |
| `/api/tuning` | GET | チューニング値取得 |
| `/api/tuning` | POST | チューニング値保存 |
| `/api/done` | POST | 再起動 |

## 参考

- https://github.com/shinyquagsire23/Switch-Fightstick
- https://github.com/Loloweb/Switch-Fightstick
- https://github.com/progmem/Switch-Fightstick
