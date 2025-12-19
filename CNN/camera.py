import serial
import time
import numpy as np
import cv2
import os

import tensorflow as tf
from tensorflow import keras
from tensorflow.keras.layers import Rescaling

# =========================
# 설정
# =========================
PORT = 'COM16'
BAUD_RATE = 256000

SAVE_DIR = "C:\\embedded_pj3\\captured_patches"  # 텍스트/디버그 이미지 저장 폴더
os.makedirs(SAVE_DIR, exist_ok=True)

MODEL_PATH = "C:\\cnn\\seat_multilabel_cnn.h5"   # 3-class softmax 모델(.h5/.keras)
IMG_SIZE = 32                                     # 모델 입력 크기
GRID_ROWS, GRID_COLS = 3, 4                       # 3x4 = 12 패치
ORIG_W, ORIG_H = 320, 240                         # 카메라 원본 크기(예: ArduCAM)
PATCH_W, PATCH_H = ORIG_W // GRID_COLS, ORIG_H // GRID_ROWS  # 80x80

CLASS_NAMES = ["PERSON", "BAG", "EMPTY"]          # softmax 출력 순서와 반드시 일치
SAVE_TXT = True                                    # 32x32 회색값을 .txt로 저장할지 여부

# 제외할 행(가운데 가로줄: 행 index = 1)
EXCLUDE_ROWS = {1}

# 제외 구역 화면 표시 방식: "black" | "white" | None
EXCLUDED_FILL = "black"  # ← 필요에 따라 "white" 또는 None 로 변경

FONT = cv2.FONT_HERSHEY_SIMPLEX
TXT_SCALE = 0.5
TXT_THICK = 1
BOX_THICK = 2


# =========================
# 모델 로드 + Rescaling 유무 감지
# =========================
def load_model_and_check():
    model = keras.models.load_model(MODEL_PATH, compile=False)
    print(f"[INFO] Model loaded: {MODEL_PATH}")

    # 마지막 Dense 유닛 수(=3) 확인
    try:
        units = model.layers[-1].units
        if units != 3:
            print(f"[WARN] Model last layer units={units}, expected 3.")
    except Exception:
        pass

    # 모델 내부 Rescaling(1/255) 사용 여부 감지
    has_rescaling = any(isinstance(l, Rescaling) for l in model.layers)
    print(f"[INFO] Model has Rescaling(1/255): {has_rescaling}")
    return model, has_rescaling


# =========================
# 시리얼
# =========================
def open_serial():
    try:
        ser = serial.Serial(PORT, BAUD_RATE, timeout=2)
        time.sleep(2)
        print(f"[INFO] Serial opened: {PORT} @ {BAUD_RATE}")
        return ser
    except Exception as e:
        print(f"[ERROR] Serial open failed: {e}")
        return None

def send_command(ser, hex_cmd):
    ser.write(bytes([hex_cmd]))
    time.sleep(0.1)

def read_image_from_serial(ser,
                           overall_timeout=12.0,
                           inter_byte_timeout=1.5,
                           min_jpeg_bytes=4000,
                           max_jpeg_bytes=500_000):
    """
    JPEG SOI(0xFFD8) ~ EOI(0xFFD9)를 스트림에서 추출.
    """
    print(f"[INFO] Waiting frame (overall={overall_timeout}s, inter={inter_byte_timeout}s)...", end="")
    t0 = time.monotonic()
    last_rx_t = t0

    buf = bytearray()
    started = False
    soi_idx = -1

    while True:
        now = time.monotonic()
        if (now - t0) > overall_timeout:
            print("\n[ERROR] Timeout (overall)")
            return None
        if (now - last_rx_t) > inter_byte_timeout and started:
            print("\n[ERROR] Timeout (inter-byte)")
            return None

        n = ser.in_waiting
        chunk = ser.read(n if n > 0 else 512)
        if chunk:
            buf += chunk
            last_rx_t = now

            if not started:
                idx = buf.find(b'\xff\xd8')
                if idx != -1:
                    buf = buf[idx:]
                    soi_idx = 0
                    started = True
                    print("\n[INFO] SOI found, receiving...", end="")
            else:
                eoi_idx = buf.rfind(b'\xff\xd9')
                if eoi_idx != -1 and eoi_idx > soi_idx:
                    frame = bytes(buf[:eoi_idx + 2])
                    if len(frame) < min_jpeg_bytes:
                        print(f"\n[WARN] Frame too small ({len(frame)} bytes).")
                        return None
                    print(f" done ({len(frame)} bytes)")
                    return frame

            if len(buf) > max_jpeg_bytes:
                print(f"\n[ERROR] Buffer overflow ({len(buf)} bytes).")
                return None
        else:
            time.sleep(0.01)


# =========================
# 패치 분할 + 전처리(모델 배치 생성)
#  - EXCLUDE_ROWS는 저장/추론에서 제외
#  - 제외된 셀은 EXCLUDED_FILL 색으로 꽉 채워 표시(또는 None이면 표시만 SKIP)
# =========================
def split_and_prepare_batches(raw_image_bgr, has_rescaling, save_txt=SAVE_TXT):
    """
    return:
      batch  : (N_included, IMG_SIZE, IMG_SIZE, 1), float32
      meta   : 포함된 각 패치의 메타(dict: row, col, x0..y1)
      visimg : 디버그 그리드를 그릴 원본 복사본(BGR)
    """
    H, W, _ = raw_image_bgr.shape
    assert (H, W) == (ORIG_H, ORIG_W), f"Unexpected image size {W}x{H}, expected {ORIG_W}x{ORIG_H}"

    visimg = raw_image_bgr.copy()
    patches = []
    meta = []
    count_included = 0
    print(f"\n[INFO] Saving patches to {SAVE_DIR} (SAVE_TXT={save_txt})")
    print(f"[INFO] Excluded rows: {sorted(list(EXCLUDE_ROWS))} (fill={EXCLUDED_FILL})")

    # 채우기 색상
    fill_color = None
    if EXCLUDED_FILL == "black":
        fill_color = (0, 0, 0)
    elif EXCLUDED_FILL == "white":
        fill_color = (255, 255, 255)

    for r in range(GRID_ROWS):
        for c in range(GRID_COLS):
            x0, y0 = c * PATCH_W, r * PATCH_H
            x1, y1 = x0 + PATCH_W, y0 + PATCH_H

            # 제외 행: 화면 처리만 하고 저장/추론 제외
            if r in EXCLUDE_ROWS:
                if fill_color is not None:
                    # 꽉 채우기
                    cv2.rectangle(visimg, (x0, y0), (x1, y1), fill_color, thickness=-1)
                    # 선택적으로 테두리(은은한 회색)
                    cv2.rectangle(visimg, (x0, y0), (x1, y1), (128, 128, 128), 1)
                else:
                    # 채우기 원치 않으면 SKIP 라벨만
                    cv2.rectangle(visimg, (x0, y0), (x1, y1), (0, 0, 255), BOX_THICK)
                    cv2.putText(visimg, "SKIP", (x0+5, y0+36), FONT, TXT_SCALE, (0, 0, 255), 2)
                continue

            # (1) 80x80 crop
            patch_bgr = raw_image_bgr[y0:y1, x0:x1]
            # (2) gray
            patch_gray = cv2.cvtColor(patch_bgr, cv2.COLOR_BGR2GRAY)
            # (3) resize -> 32x32
            patch_32 = cv2.resize(patch_gray, (IMG_SIZE, IMG_SIZE), interpolation=cv2.INTER_AREA)
            patch_32 = patch_32.astype(np.float32)  # [0,255]

            # (4) txt 저장(옵션) — 포함된 패치만 저장
            if save_txt:
                txt_path = os.path.join(SAVE_DIR, f"patch_{count_included}.txt")
                np.savetxt(txt_path, patch_32, fmt='%d', delimiter=' ')
                print(f"  - saved: patch_{count_included}.txt (row={r}, col={c})")

            # (5) 배치/메타
            patches.append(patch_32[..., None])
            meta.append({"row": r, "col": c, "x0": x0, "y0": y0, "x1": x1, "y1": y1})
            count_included += 1

            # 포함된 셀: 초록 박스 + 인덱스
            cv2.rectangle(visimg, (x0, y0), (x1, y1), (0, 255, 0), BOX_THICK)
            cv2.putText(visimg, f"{count_included-1}", (x0+5, y0+18), FONT, 0.6, (0, 255, 255), 2)

    if not patches:
        raise RuntimeError("No included cells. EXCLUDE_ROWS가 모든 행을 제외했는지 확인하세요.")

    print(f"[INFO] Patch split done. Included={len(patches)}, Excluded={GRID_COLS*len(EXCLUDE_ROWS)}\n")
    batch = np.stack(patches, axis=0)  # (N_included, 32, 32, 1)

    # 모델에 Rescaling 레이어가 없으면 여기서 1/255 수행
    if not has_rescaling:
        batch *= (1.0 / 255.0)

    return batch, meta, visimg


# =========================
# 추론 + 오버레이 (라벨만 표시)
# =========================
def infer_and_overlay(model, batch, visimg, meta):
    """
    model: 3-class softmax
    batch: (N_included,32,32,1)
    meta : 포함된 패치들의 메타(dict)
    visimg: BGR image to draw on
    """
    preds = model.predict(batch, verbose=0)  # (N_included, 3)
    if preds.shape[1] != 3:
        raise RuntimeError(f"Model output shape {preds.shape} != (N,3). 3-class model 필요.")

    print("[RESULT] Seat states per included patch:")
    print("Idx | (row,col) | bbox(x0,y0,x1,y1)      | label")

    for i, (p, info) in enumerate(zip(preds, meta)):
        x0, y0, x1, y1 = info["x0"], info["y0"], info["x1"], info["y1"]
        r, c = info["row"], info["col"]

        cls_idx = int(np.argmax(p))
        label = CLASS_NAMES[cls_idx]

        # 콘솔 로그
        print(f"{i:3d} | ({r},{c})     | ({x0:4d},{y0:3d})-({x1:4d},{y1:3d}) | {label}")

        # 오버레이(라벨만)
        text = f"{label}"
        tx, ty = x0 + 5, y0 + 36
        cv2.putText(visimg, text, (tx+1, ty+1), FONT, TXT_SCALE, (0, 0, 0), TXT_THICK+2)
        cv2.putText(visimg, text, (tx, ty),     FONT, TXT_SCALE, (0, 255, 0), TXT_THICK)

    return visimg


# =========================
# 메인 루프
# =========================
def main():
    ser = open_serial()
    if ser is None:
        return
    model, has_rescaling = load_model_and_check()

    print("\n--- ArduCAM → patches → CNN inference (3-class, label-only, middle row filled) ---")
    print(f"Excluded rows: {sorted(list(EXCLUDE_ROWS))} (fill={EXCLUDED_FILL})")
    print("'c': 촬영/수신 후 추론 & 오버레이 표시")
    print("'q': 종료")
    ser.reset_output_buffer()
    ser.reset_input_buffer()

    while True:
        cmd = input("\n명령 >> ").strip().lower()
        if cmd == 'q':
            break
        elif cmd == 'c':
            # 캡처 전 버퍼 리셋
            ser.reset_output_buffer()
            ser.reset_input_buffer()
            time.sleep(0.02)

            send_command(ser, 0x10)  # 보드 펌웨어의 캡처 트리거 명령
            jpg = read_image_from_serial(ser)
            if jpg is None:
                print("[INFO] Frame receive failed. Check BAUD/latency/frame-size.")
                continue

            # 디코드
            arr = np.frombuffer(jpg, dtype=np.uint8)
            frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)  # BGR
            if frame is None:
                print("[ERROR] JPEG decode failed.")
                continue

            # 320x240 보정
            if frame.shape[1] != ORIG_W or frame.shape[0] != ORIG_H:
                frame = cv2.resize(frame, (ORIG_W, ORIG_H), interpolation=cv2.INTER_AREA)

            # 패치 분할(+중간 행 제외/채움) + 배치 준비
            batch, meta, visimg = split_and_prepare_batches(frame, has_rescaling, save_txt=SAVE_TXT)

            # 추론 + 오버레이 (라벨만)
            visimg = infer_and_overlay(model, batch, visimg, meta)

            # 표시
            cv2.imshow("Captured + Grid + Predictions (Label Only, Middle Row Filled)", visimg)
            cv2.waitKey(1)

        elif cmd.isdigit():
            send_command(ser, int(cmd))

    ser.close()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
