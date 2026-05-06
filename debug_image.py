#!/usr/bin/env python3
"""
image_data.c のバイト列を画像に復元して保存するデバッグスクリプト。
plate.png と debug_reconstructed.png を目視比較してズレを確認する。
"""

import re
import sys
from PIL import Image

IMAGE_WIDTH = 320
IMAGE_HEIGHT = 120
BYTES_PER_ROW = IMAGE_WIDTH // 8  # 40

# ── image_data.c からバイト列を読み込む ──────────────────────────
def load_image_data_c(path):
    with open(path, "r") as f:
        src = f.read()

    # { ... } の中身だけ抽出
    m = re.search(r'\{(.*?)\}', src, re.DOTALL)
    if not m:
        raise ValueError("image_data.c の {} ブロックが見つかりません")

    hex_tokens = re.findall(r'0x[0-9a-fA-F]+', m.group(1))
    return [int(t, 16) for t in hex_tokens]


# ── バイト列を PIL 画像に変換 ────────────────────────────────────
def bytes_to_image(data):
    img = Image.new("RGB", (IMAGE_WIDTH, IMAGE_HEIGHT), "white")
    pixels = img.load()

    for y in range(IMAGE_HEIGHT):
        for x in range(IMAGE_WIDTH):
            byte_idx = y * BYTES_PER_ROW + x // 8
            bit = data[byte_idx] & (1 << (x % 8))
            # bit=1 → インク (黒), bit=0 → 無し (白)
            pixels[x, y] = (0, 0, 0) if bit else (255, 255, 255)

    return img


# ── 差分チェック ─────────────────────────────────────────────────
def check_diff_with_original(reconstructed, original_path):
    try:
        orig = Image.open(original_path).convert("RGB")
    except FileNotFoundError:
        print(f"  [skip] {original_path} が見つかりません")
        return

    if orig.size != reconstructed.size:
        print(f"  [warn] サイズ不一致: original={orig.size}, reconstructed={reconstructed.size}")
        return

    orig_px = orig.load()
    rec_px = reconstructed.load()
    diff_count = 0
    for y in range(IMAGE_HEIGHT):
        for x in range(IMAGE_WIDTH):
            # 白 or 黒に閾値で丸めて比較
            orig_ink = orig_px[x, y][0] < 128
            rec_ink = rec_px[x, y][0] < 128
            if orig_ink != rec_ink:
                diff_count += 1

    total = IMAGE_WIDTH * IMAGE_HEIGHT
    print(f"  plate.png との差分ピクセル数: {diff_count} / {total} ({diff_count*100/total:.2f}%)")
    if diff_count == 0:
        print("  ✓ 完全一致 — image_data.c のデータにズレはありません")
    else:
        print("  ! 不一致あり — データ変換に問題がある可能性があります")


def main():
    data_path = "src/image_data.c"
    original_path = "plate.png"
    out_path = "debug_reconstructed.png"

    print(f"[1] {data_path} を読み込み中...")
    data = load_image_data_c(data_path)
    print(f"    バイト数: {len(data)} (期待値: {BYTES_PER_ROW * IMAGE_HEIGHT + 1} + 末尾 0x0)")

    print("[2] 画像に復元中...")
    img = bytes_to_image(data)

    print(f"[3] {out_path} に保存...")
    img.save(out_path)
    print(f"    保存完了 → {out_path}")

    print("[4] オリジナルと差分チェック...")
    check_diff_with_original(img, original_path)

    # ── 途中経過プレビュー: 上半分・下半分・左右別に分割保存 ────
    print("[5] デバッグ用分割画像を保存...")
    halves = {
        "debug_top_half.png":    img.crop((0, 0, IMAGE_WIDTH, IMAGE_HEIGHT // 2)),
        "debug_bottom_half.png": img.crop((0, IMAGE_HEIGHT // 2, IMAGE_WIDTH, IMAGE_HEIGHT)),
        "debug_left_half.png":   img.crop((0, 0, IMAGE_WIDTH // 2, IMAGE_HEIGHT)),
        "debug_right_half.png":  img.crop((IMAGE_WIDTH // 2, 0, IMAGE_WIDTH, IMAGE_HEIGHT)),
    }
    for fname, region in halves.items():
        region_4x = region.resize((region.width * 4, region.height * 4), Image.NEAREST)
        region_4x.save(fname)
        print(f"    {fname} (4x拡大)")

    print("\n完了。debug_reconstructed.png と plate.png を目視比較してください。")


if __name__ == "__main__":
    main()
