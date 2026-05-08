Switch Fightstick (AtomS3) 配布用バイナリ

セットアップガイド
- macOS: `MAC-SETUP.md`
- Windows: `WINDOWS-SETUP.md`

重要
- `splatoon-atoms3-merged.bin` は組み込み画像を含んだ配布用ファームです。
- Web UI で画像をアップロードしたことがある個体では、保存済みの `/image.bin` が優先されます。
- 組み込み画像を使いたい場合は、Web UI の `Use built-in image` を押してください。
ファイル一覧
- splatoon-atoms3-merged.bin
  1本化済みイメージ。オフセット 0x0 に書き込んでください。

- flash_merged.sh
  1本化済みイメージを書き込む自動スクリプト。
  引数なしなら /dev/cu.usbmodem* を自動検出して書き込みます。

- flash_merged.bat
  Windows (cmd) 用の自動書き込みスクリプト。
  引数なしなら USB シリアルポートを自動検出します。

- flash_merged.ps1
  Windows (PowerShell) 用の自動書き込みスクリプト。
  引数なしなら USB シリアルポートを自動検出します。

- splatoon-atoms3-firmware.bin
  アプリ本体イメージのみ。オフセット 0x10000 に書き込みます。
  （既存の bootloader / partitions が必要です）

- bootloader.bin
  ブートローダーイメージ（オフセット 0x0）

- partitions.bin
  パーティションテーブルイメージ（オフセット 0x8000）

推奨書き込みコマンド（1本化イメージ）
- esptool.py --chip esp32s3 --port <PORT> --baud 460800 write_flash 0x0 splatoon-atoms3-merged.bin

最も簡単な方法（自動スクリプト）
- ./flash_merged.sh
- ./flash_merged.sh /dev/cu.usbmodem1101

Windows での簡単な方法（自動スクリプト）
- flash_merged.bat
- flash_merged.bat COM3
- powershell -ExecutionPolicy Bypass -File .\flash_merged.ps1
- powershell -ExecutionPolicy Bypass -File .\flash_merged.ps1 COM3

注意
- 上記スクリプトの実行には、esptool.py (または esptool) が PATH にある必要があります。
- AtomS3 に保存済みのカスタム画像がある場合、組み込み画像よりそちらが優先されます。

別案の書き込みコマンド（分割イメージ）
- esptool.py --chip esp32s3 --port <PORT> --baud 460800 write_flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 splatoon-atoms3-firmware.bin

このプロジェクトで使用している PlatformIO コマンド
- /Users/katano/Library/Python/3.9/bin/pio run -t upload --upload-port /dev/cu.usbmodem1101
