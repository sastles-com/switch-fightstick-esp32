# AtomS3 書き込み手順（macOS向け）

この手順は、配布ファイル `splatoon-dist.zip` を使って
M5Stack AtomS3 にファームを書き込むための初心者向けガイドです。

---

## 0. 事前に用意するもの

- Mac（macOS）
- M5Stack AtomS3
- USB-C ケーブル（データ通信対応）
- 配布ファイル: `splatoon-dist.zip`

注意:
- 充電専用ケーブルでは書き込みできません。
- Switch には接続せず、最初は Mac に直接つないでください。

---

## 1. 必要アプリをインストール

### 1-1. Homebrew をインストール（未導入の場合のみ）

ターミナルを開いて以下を実行:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

インストール確認:

```bash
brew --version
```

### 1-2. Python3 をインストール

```bash
brew install python
```

確認:

```bash
python3 --version
```

### 1-3. esptool をインストール

```bash
python3 -m pip install --user esptool
```

確認:

```bash
esptool.py version
```

もし `esptool.py: command not found` が出る場合:

```bash
python3 -m site --user-base
```

表示されたパスの `bin` を `PATH` に追加してください。
（例: `~/.zprofile` に追記）

```bash
PY_USER_BIN="$(python3 -c 'import site; print(site.getuserbase() + "/bin")')"
echo "export PATH=\"$PY_USER_BIN:\$PATH\"" >> ~/.zprofile
source ~/.zprofile
```

※ シェル再起動後に再確認してください。

---

## 2. 配布ZIPを展開

`Downloads` に置いた場合の例:

```bash
cd ~/Downloads
unzip splatoon-dist.zip -d splatoon-dist
cd splatoon-dist
ls
```

以下のようなファイルが見えればOK:

- `splatoon-atoms3-merged.bin`
- `flash_merged.sh`
- `MAC-SETUP.md`
- `README-distribution.txt`

---

## 3. AtomS3 を Mac に接続

USB-C ケーブルで AtomS3 を Mac に接続。

接続確認:

```bash
ls /dev/cu.usbmodem*
```

例:
- `/dev/cu.usbmodem1101`

---

## 4. 自動スクリプトで書き込み（推奨）

### 4-1. 実行権限を付与

```bash
chmod +x flash_merged.sh
```

### 4-2. 書き込み実行

```bash
./flash_merged.sh
```

ポート自動検出が失敗した場合:

```bash
./flash_merged.sh /dev/cu.usbmodem1101
```

`Done.` が出たら書き込み完了です。

---

## 5. 動作確認

1. AtomS3 をいったん抜く
2. Switch へ接続
3. 画面に `Ready` が出ることを確認

組み込み画像 (`plate.png`) を確認したいのに以前アップロードした画像が出る場合:

1. BtnA を 2 秒長押しして Web UI を開く
2. `Use built-in image` を押す
3. 再起動後に組み込み画像で動作することを確認する

---

## 6. うまくいかない時

### 6-1. 接続できない / ポートが出ない

- RESETボタンを長押しして「書き込み可能モード」にする必要がある。
- ケーブルを交換（データ通信対応）
- 別のUSBポートへ挿す
- いったん抜いて挿し直す

### 6-2. `esptool` が見つからない

- `python3 -m pip install --user esptool` を再実行
- `PATH` 設定を見直す

### 6-3. 書き込み失敗する

手動コマンドで実行して詳細ログを確認:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 460800 write_flash 0x0 splatoon-atoms3-merged.bin
```

---

## 7. 補足

- この配布では `splatoon-atoms3-merged.bin`（1本化イメージ）を使用しています。
- 基本的にオフセットは `0x0` 固定です。
- 既存環境を気にせず上書きしやすい方法です。
- ただし Web UI で保存済みのカスタム画像 (`/image.bin`) は消えません。
