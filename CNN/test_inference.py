import os
import numpy as np
import tensorflow as tf
from tensorflow import keras

# =========================
# 설정
# =========================
IMG_SIZE = 32
MODEL_PATH = "C:\\cnn\\seat_multilabel_cnn.h5"

# 여기만 바꿔서 테스트 대상 지정
# - 폴더: 폴더 내 이미지 전체 테스트
# - .npy 파일: grid에서 저장한 cells.npy
# - 일반 이미지 파일: 단일 이미지
TARGET_PATH = "C:\\cnn\\cells.npy"

VALID_EXT = (".jpg", ".jpeg", ".png", ".bmp")

# grid와 임계값 통일
PERSON_THRESH = 0.8
BAG_THRESH    = 0.85


# =========================
# 모델 로드
# =========================
def load_model():
    model = keras.models.load_model(MODEL_PATH, compile=False)
    print(f"[INFO] Loaded model from {MODEL_PATH}")
    return model


# =========================
# 좌석 상태 판정
# =========================
def probs_to_label(person_prob, bag_prob,
                   person_thresh=PERSON_THRESH,
                   bag_thresh=BAG_THRESH):
    if person_prob >= person_thresh:
        return "PERSON"
    elif bag_prob >= bag_thresh:
        return "BAG"
    else:
        return "EMPTY"


# =========================
# 이미지 전처리 (단일 파일용)
# =========================
def preprocess_image(img_path):
    img = keras.utils.load_img(
        img_path,
        color_mode="grayscale",
        target_size=(IMG_SIZE, IMG_SIZE),
    )
    img_array = keras.utils.img_to_array(img)
    img_array = np.expand_dims(img_array, axis=0)
    return img_array


# =========================
# 단일 이미지 예측
# =========================
def predict_image(model, img_path):
    img = preprocess_image(img_path)
    preds = model.predict(img, verbose=0)
    person_prob = float(preds[0][0])
    bag_prob    = float(preds[0][1])

    label = probs_to_label(person_prob, bag_prob)
    return label, person_prob, bag_prob


# =========================
# 폴더 전체 테스트
# =========================
def test_folder(model, folder_path):
    files = [
        f for f in os.listdir(folder_path)
        if f.lower().endswith(VALID_EXT)
    ]
    files.sort()
    if not files:
        print("[WARN] 폴더 안에 이미지 파일이 없습니다.")
        return

    counts = {"PERSON": 0, "BAG": 0, "EMPTY": 0}
    print(f"[INFO] 폴더 내 이미지 개수: {len(files)}\n")

    for fname in files:
        fpath = os.path.join(folder_path, fname)
        label, p_prob, b_prob = predict_image(model, fpath)

        counts[label] += 1

        print(f"{fname:30s} -> {label:6s}  "
              f"(person={p_prob:.3f}, bag={b_prob:.3f})")

    print("\n[SUMMARY]")
    print(f"PERSON: {counts['PERSON']}")
    print(f"BAG   : {counts['BAG']}")
    print(f"EMPTY : {counts['EMPTY']}")


# =========================
# npy 셀 배열 테스트 (grid와 동일 입력)
# =========================
def test_cells_npy(model, npy_path):
    cells = np.load(npy_path)  # (N, H, W, 1)
    print(f"[INFO] Loaded cells from {npy_path}, shape={cells.shape}")

    preds = model.predict(cells, verbose=0)  # (N, 2)

    print("\n[CELLS.NPY RESULT]")
    print("Index | label    | person  | bag")
    for i, p in enumerate(preds):
        person_prob = float(p[0])
        bag_prob    = float(p[1])
        label = probs_to_label(person_prob, bag_prob)
        print(f"{i:5d} | {label:7s} | {person_prob:7.3f} | {bag_prob:7.3f}")


# =========================
# 메인
# =========================
if __name__ == "__main__":
    model = load_model()

    if os.path.isfile(TARGET_PATH) and TARGET_PATH.lower().endswith(".npy"):
        # grid에서 저장한 cells.npy 테스트
        test_cells_npy(model, TARGET_PATH)

    elif os.path.isdir(TARGET_PATH):
        # 폴더 전체 테스트
        test_folder(model, TARGET_PATH)

    elif os.path.isfile(TARGET_PATH):
        # 단일 이미지 테스트
        label, p_prob, b_prob = predict_image(model, TARGET_PATH)
        print(f"person_prob = {p_prob:.4f}, bag_prob = {b_prob:.4f}")
        print(f"Prediction for '{TARGET_PATH}': {label}")

    else:
        print(f"[ERROR] TARGET_PATH not found: {TARGET_PATH}")
