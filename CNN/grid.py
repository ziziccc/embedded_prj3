## 입력 이미지 분할 및 roi 형성 코드


import os
import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras.preprocessing.image import array_to_img

# =========================
# 설정
# =========================
IMG_SIZE = 32  # 학습 때 쓴 입력 크기
MODEL_PATH = "C:\\cnn\\seat_multilabel_cnn.h5"   # 3-class softmax 모델(.h5/.keras)
FULL_IMAGE_PATH = "C:\\cnn\\test1.png"           # 2x4 전체 이미지 경로
CELLS_NPY_PATH = "C:\\cnn\\cells.npy"            # 잘라낸 셀 저장 경로

# 2 x 4 그리드 (4행, 2열)
GRID_ROWS = 4
GRID_COLS = 2

CLASS_NAMES = ["PERSON", "BAG", "EMPTY"]  # softmax 출력 순서와 일치해야 함

# =========================
# 모델 로드
# =========================
def load_model():
    model = keras.models.load_model(MODEL_PATH, compile=False)
    print(f"[INFO] Loaded model from {MODEL_PATH}")
    # 안전장치: 마지막 Dense 유닛 수 확인
    try:
        units = model.layers[-1].units
        if units != 3:
            print(f"[WARN] Model last layer units={units}, expected=3. "
                  "확인 필요(softmax 3-class 모델인지).")
    except Exception:
        pass
    return model

# =========================
# 전체 이미지 -> 그리드 셀 분할 (+디버그 저장)
# =========================
def split_image_to_cells(img_path,
                         grid_rows=GRID_ROWS,
                         grid_cols=GRID_COLS,
                         img_size=IMG_SIZE):
    """
    return:
      cells: (N, img_size, img_size, 1)
      meta : 각 셀 정보 dict 리스트
    """
    img = keras.utils.load_img(
        img_path,
        color_mode="grayscale"
    )
    img_arr = keras.utils.img_to_array(img)  # (H, W, 1), float32 [0,255]
    H, W, _ = img_arr.shape
    print(f"[INFO] Full image shape: H={H}, W={W}")

    cells = []
    meta = []

    cell_h = H / grid_rows
    cell_w = W / grid_cols

    debug_dir = r"C:\cnn\_debug_cells"
    os.makedirs(debug_dir, exist_ok=True)

    for r in range(grid_rows):
        for c in range(grid_cols):
            y0 = int(round(r * cell_h))
            y1 = int(round((r + 1) * cell_h))
            x0 = int(round(c * cell_w))
            x1 = int(round((c + 1) * cell_w))

            y0 = max(0, min(y0, H - 1))
            y1 = max(0, min(y1, H))
            x0 = max(0, min(x0, W - 1))
            x1 = max(0, min(x1, W))

            if y1 <= y0 or x1 <= x0:
                print(f"[WARN] invalid cell crop: r={r}, c={c}, "
                      f"({x0},{y0})-({x1},{y1})")
                continue

            cell = img_arr[y0:y1, x0:x1, :]  # (h_cell, w_cell, 1)

            # 모델에 맞게 리사이즈 (모델 내부 Rescaling이 1/255 수행)
            cell_resized = tf.image.resize(cell, (img_size, img_size))
            cell_resized = tf.cast(cell_resized, tf.float32).numpy()

            # 디버그 저장
            debug_img = array_to_img(cell_resized)
            debug_img.save(os.path.join(debug_dir, f"cell_{r}_{c}.png"))

            cells.append(cell_resized)
            meta.append({
                "row": r,
                "col": c,
                "x0": x0, "y0": y0,
                "x1": x1, "y1": y1
            })

    if not cells:
        raise RuntimeError("No cells were extracted from the image.")

    cells = np.stack(cells, axis=0)  # (N, IMG_SIZE, IMG_SIZE, 1)
    print(f"[INFO] Extracted {cells.shape[0]} cells: {cells.shape}")
    return cells, meta

# =========================
# 메인
# =========================
def main():
    if not os.path.exists(FULL_IMAGE_PATH):
        print(f("[ERROR] FULL_IMAGE_PATH not found: {FULL_IMAGE_PATH}"))
        return

    model = load_model()

    # 1) 전체 이미지를 셀로 분해
    cells, meta = split_image_to_cells(FULL_IMAGE_PATH)

    # 1-1) 셀 npy로 저장
    np.save(CELLS_NPY_PATH, cells)
    print(f"[INFO] Saved cells to {CELLS_NPY_PATH}")

    # 2) 예측 (softmax 확률 3개)
    preds = model.predict(cells, verbose=0)  # (N, 3) expected

    if preds.shape[1] != 3:
        print(f"[ERROR] Model output shape {preds.shape} != (N, 3). "
              "3-class softmax 모델을 로드했는지 확인하세요.")
        return

    print("\n[RESULT] Seat states per cell:")
    print("Index | (row,col) | bbox(x0,y0,x1,y1)     | label    | P(person) | P(bag) | P(empty)")

    for i, (p, info) in enumerate(zip(preds, meta)):
        # p: [p_person, p_bag, p_empty], 합=1
        cls_idx = int(np.argmax(p))
        label = CLASS_NAMES[cls_idx]
        person_prob = float(p[0])
        bag_prob    = float(p[1])
        empty_prob  = float(p[2])

        print(
            f"{i:5d} | "
            f"({info['row']},{info['col']}) | "
            f"({info['x0']:4d},{info['y0']:4d})-({info['x1']:4d},{info['y1']:4d}) | "
            f"{label:7s} | "
            f"{person_prob:9.3f} | {bag_prob:6.3f} | {empty_prob:7.3f}"
        )

if __name__ == "__main__":
    main()

