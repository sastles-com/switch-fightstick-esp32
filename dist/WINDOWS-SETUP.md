# AtomS3 書き込み手順（Windows向け）

この手順は、配布ファイル `splatoon-dist.zip` を使って
M5Stack AtomS3 にファームを書き込むための初心者向けガイドです。

---

## 0. 事前に用意するもの

- Windows PC
- M5Stack AtomS3
- USB-C ケーブル（データ通信対応）
- 配布ファイル: `splatoon-dist.zip`

注意:
- 充電専用ケーブルでは書き込みできません。
- Switch には接続せず、最初は Windows PC に直接つないでください。

---

## 1. 必要アプリをインストール

### 1-1. Python をインストール

1. 公式サイトにアクセス: https://www.python.org/downloads/windows/
2. 最新版 Python 3.x をダウンロードして実行
3. インストール画面で **Add Python to PATH** にチェック
4. Install Now を実行

インストール確認（コマンドプロンプト）:

```bat
python --version
```

### 1-2. esptool をインストール

コマンドプロンプトで実行:

```bat
python -m pip install --user esptool
```

確認:

```bat
esptool.py version
```

もし `esptool.py` が見つからない場合:

```bat
python -m site --user-base
```

表示されたパスの `Scripts` を環境変数 `Path` に追加してください。
例: `C:\Users\<ユーザー名>\AppData\Roaming\Python\Python3x\Scripts`

---

## 2. 配布ZIPを展開

`Downloads` に置いた場合の例（PowerShell）:

```powershell
cd $HOME\Downloads
Expand-Archive .\splatoon-dist.zip -DestinationPath .\splatoon-dist -Force
cd .\splatoon-dist
Get-ChildItem
```

以下のようなファイルが見えればOK:

- `splatoon-atoms3-merged.bin`
- `flash_merged.bat`
- `flash_merged.ps1`
- `README-distribution.txt`

---

## 3. AtomS3 を Windows に接続

USB-C ケーブルで AtomS3 を Windows PC に接続。

COMポート確認（PowerShell）:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name
```

例:
- `COM3`
- `COM5`

---

## 4. 自動スクリプトで書き込み（推奨）

### 4-1. cmd で実行する方法

1. `splatoon-dist` フォルダを開く
2. アドレスバーに `cmd` と入力して Enter
3. 以下を実行

```bat
flash_merged.bat
```

ポート自動検出が失敗した場合:

```bat
flash_merged.bat COM3
```

### 4-2. PowerShell で実行する方法

```powershell
powershell -ExecutionPolicy Bypass -File .\flash_merged.ps1
```

ポートを指定する場合:

```powershell
powershell -ExecutionPolicy Bypass -File .\flash_merged.ps1 COM3
```

`Done.` が出たら書き込み完了です。

---

## 5. 動作確認

1. AtomS3 をいったん抜く
2. Switch へ接続
3. 画面に `Ready` が出ることを確認

---

## 6. うまくいかない時

### 6-1. COMポートが出ない

- RESETボタンを長押しして「書き込み可能モード」にする必要がある。
- ケーブルを交換（データ通信対応）
- 別のUSBポートへ挿す
- いったん抜いて挿し直す
- デバイスマネージャーを開いて「ポート (COM と LPT)」を確認

### 6-2. `esptool` が見つからない

- `python -m pip install --user esptool` を再実行
- `Path` 環境変数に Python Scripts パスを追加
- 新しいターミナルを開き直して再実行

### 6-3. 書き込み失敗する

手動コマンドで実行して詳細ログを確認:

```bat
esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash 0x0 splatoon-atoms3-merged.bin
```

---

## 7. 補足

- この配布では `splatoon-atoms3-merged.bin`（1本化イメージ）を使用しています。
- 基本的にオフセットは `0x0` 固定です。
- 既存環境を気にせず上書きしやすい方法です。
