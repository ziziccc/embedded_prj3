##  keras 모델과 numpy 가중치 파일 비교 테스트 코드 


import os
import numpy as np
from tensorflow import keras

# =========================
# 설정 부분 
# =========================
IMG_SIZE = 32

MODEL_PATH = "C:\\cnn\\seat_multilabel_cnn.h5"   # 또는 .keras
EXPORT_DIR = "C:\\cnn\\export"                   # export_weights_for_zybo.py에서 쓴 폴더

TEST_IMAGE_PATH = "C:\\dataset\\val\\bag\\7_534_bag_000.jpg"  # 테스트 이미지 파일 경로

# 3-class 이름 (사람/가방/빈자리)
CLASS_NAMES = ["PERSON", "BAG", "EMPTY"]
PERSON_IDX = 0
BAG_IDX = 1
EMPTY_IDX = 2


# =========================
# 유틸: 이미지 로드 & 전처리
# =========================
def load_and_preprocess_image(path):
    """
    - Grayscale
    - 32x32 resize
    - float32 (0~255), 여기서는 /255 안 함!
    - shape: (1, 32, 32, 1)
    """
    img = keras.utils.load_img(
        path,
        color_mode="grayscale",
        target_size=(IMG_SIZE, IMG_SIZE),
    )
    x = keras.utils.img_to_array(img)   # (32,32,1), [0,255]
    x = x.astype("float32")
    x = np.expand_dims(x, axis=0)       # (1,32,32,1)
    return x


# =========================
# 유틸: Conv/Pool/Dense 순수 NumPy 구현
# =========================
def conv2d_same(x, W, b, stride=1):
    """
    x: (H, W, C_in)
    W: (KH, KW, C_in, C_out)
    b: (C_out,)
    padding="same", strides=1 가정
    ReLU는 따로 적용
    """
    H, W_in, C_in = x.shape
    KH, KW, C_in_w, C_out = W.shape
    assert C_in == C_in_w

    pad_h = KH // 2
    pad_w = KW // 2

    x_padded = np.zeros((H + 2*pad_h, W_in + 2*pad_w, C_in), dtype=np.float32)
    x_padded[pad_h:pad_h+H, pad_w:pad_w+W_in, :] = x

    out = np.zeros((H, W_in, C_out), dtype=np.float32)

    for oh in range(H):
        for ow in range(W_in):
            region = x_padded[oh:oh+KH, ow:ow+KW, :]
            for oc in range(C_out):
                out[oh, ow, oc] = np.sum(region * W[..., oc]) + b[oc]

    return out


def relu(x):
    return np.maximum(x, 0.0)


def maxpool2x2(x):
    """
    x: (H, W, C)
    pool size 2x2, stride 2
    """
    H, W, C = x.shape
    assert H % 2 == 0 and W % 2 == 0
    H_out = H // 2
    W_out = W // 2

    out = np.zeros((H_out, W_out, C), dtype=np.float32)
    for oh in range(H_out):
        for ow in range(W_out):
            region = x[oh*2:oh*2+2, ow*2:ow*2+2, :]
            out[oh, ow, :] = np.max(region, axis=(0, 1))
    return out


def dense(x, W, b, activation=None):
    """
    x: (N,) 1D 벡터
    W: (N, M)
    b: (M,)
    """
    y = x @ W + b
    if activation == "relu":
        y = relu(y)
    elif activation == "sigmoid":
        y = 1.0 / (1.0 + np.exp(-y))
    return y


# =========================
# multilabel 예측 → 3-class 라벨
# =========================
def multilabel_pred_to_class(person_prob, bag_prob,
                             person_thresh=0.5, bag_thresh=0.5):
    """
    예측 확률 [person_prob, bag_prob] -> 3-class 인덱스
    규칙:
      1) person_prob >= person_thresh → PERSON
      2) 그 외에서 bag_prob >= bag_thresh → BAG
      3) 둘 다 미만 → EMPTY
    """
    if person_prob >= person_thresh:
        return PERSON_IDX
    elif bag_prob >= bag_thresh:
        return BAG_IDX
    else:
        return EMPTY_IDX


# =========================
# 1) Keras 모델 vs .npy weight 일치 여부 확인
# =========================
def check_weights_match(model, export_dir):
    print("==== [1] Checking Keras weights vs exported .npy ====")

    for layer in model.layers:
        w = layer.get_weights()
        if len(w) == 0:
            continue  # Rescaling 등

        if len(w) != 2:
            print(f"[WARN] Layer {layer.name}: unexpected len(weights)={len(w)} -> skip")
            continue

        W_keras, B_keras = w
        W_keras = np.asarray(W_keras, dtype=np.float32)
        B_keras = np.asarray(B_keras, dtype=np.float32)

        w_path = os.path.join(export_dir, f"{layer.name}_W.npy")
        b_path = os.path.join(export_dir, f"{layer.name}_B.npy")

        if not (os.path.exists(w_path) and os.path.exists(b_path)):
            print(f"[WARN] NPY not found for layer {layer.name}, skip")
            continue

        W_npy = np.load(w_path).astype(np.float32)
        B_npy = np.load(b_path).astype(np.float32)

        if W_npy.shape != W_keras.shape or B_npy.shape != B_keras.shape:
            print(f"[ERR] shape mismatch at layer {layer.name}: "
                  f"Keras W{W_keras.shape}, npy W{W_npy.shape}, "
                  f"Keras B{B_keras.shape}, npy B{B_npy.shape}")
            continue

        w_diff = np.max(np.abs(W_keras - W_npy))
        b_diff = np.max(np.abs(B_keras - B_npy))

        print(f"Layer {layer.name:10s}: "
              f"max|W_keras - W_npy| = {w_diff:.6e}, "
              f"max|B_keras - B_npy| = {b_diff:.6e}")

    print("==== [1] Check done ====\n")


# =========================
# 2) 단일 이미지에 대해
#    - Keras 모델 예측
#    - NumPy 순수 연산 예측
#    + 사람이냐/가방이냐/빈자리냐 출력
# =========================
def run_single_image_compare(model, export_dir, image_path):
    print("==== [2] Single image inference compare ====")
    print(f"[INFO] Test image: {image_path}")

    # ---- (1) 이미지 로드 & Keras 예측 ----
    x_raw = load_and_preprocess_image(image_path)   # (1,32,32,1), [0,255]
    y_keras = model.predict(x_raw)                  # Rescaling 포함
    yk = y_keras[0]                                 # (2,)

    print(f"Keras predict output (person_prob, bag_prob): {yk}")

    # 3-class 라벨
    k_cls = multilabel_pred_to_class(yk[0], yk[1])
    print(f"Keras predicted class: {CLASS_NAMES[k_cls]}")

    # ---- (2) NumPy 순수 연산 경로 준비 ----
    x_np = (x_raw[0] / 255.0).astype(np.float32)    # (32,32,1), [0,1]

    W_c1 = np.load(os.path.join(export_dir, "conv2d_W.npy")).astype(np.float32)
    B_c1 = np.load(os.path.join(export_dir, "conv2d_B.npy")).astype(np.float32)

    W_c2 = np.load(os.path.join(export_dir, "conv2d_1_W.npy")).astype(np.float32)
    B_c2 = np.load(os.path.join(export_dir, "conv2d_1_B.npy")).astype(np.float32)

    W_c3 = np.load(os.path.join(export_dir, "conv2d_2_W.npy")).astype(np.float32)
    B_c3 = np.load(os.path.join(export_dir, "conv2d_2_B.npy")).astype(np.float32)

    W_d1 = np.load(os.path.join(export_dir, "dense_W.npy")).astype(np.float32)
    B_d1 = np.load(os.path.join(export_dir, "dense_B.npy")).astype(np.float32)

    W_d2 = np.load(os.path.join(export_dir, "dense_1_W.npy")).astype(np.float32)
    B_d2 = np.load(os.path.join(export_dir, "dense_1_B.npy")).astype(np.float32)

    # ---- (3) Conv1 + ReLU + Pool ----
    h1 = conv2d_same(x_np, W_c1, B_c1)   # (32,32,16)
    h1 = relu(h1)
    h1 = maxpool2x2(h1)                  # (16,16,16)

    # ---- (4) Conv2 + ReLU + Pool ----
    h2 = conv2d_same(h1, W_c2, B_c2)     # (16,16,32)
    h2 = relu(h2)
    h2 = maxpool2x2(h2)                  # (8,8,32)

    # ---- (5) Conv3 + ReLU + Pool ----
    h3 = conv2d_same(h2, W_c3, B_c3)     # (8,8,64)
    h3 = relu(h3)
    h3 = maxpool2x2(h3)                  # (4,4,64)

    # ---- (6) Flatten ----
    h3_flat = h3.reshape(-1)             # (4*4*64,) = (1024,)

    # ---- (7) Dense1 + ReLU ----
    h4 = dense(h3_flat, W_d1, B_d1, activation="relu")  # (64,)

    # ---- (8) Dense2 + Sigmoid ----
    y_np = dense(h4, W_d2, B_d2, activation="sigmoid")  # (2,)
    print(f"NumPy manual output (person_prob, bag_prob): {y_np}")

    n_cls = multilabel_pred_to_class(y_np[0], y_np[1])
    print(f"NumPy predicted class: {CLASS_NAMES[n_cls]}")

    # ---- (9) 차이 확인 ----
    diff = np.max(np.abs(yk - y_np))
    print(f"max |Keras - NumPy| = {diff:.6e}")
    print("==== [2] Compare done ====")


# =========================
# 메인
# =========================
def main():
    model = keras.models.load_model(MODEL_PATH)
    model.summary()

    check_weights_match(model, EXPORT_DIR)
    run_single_image_compare(model, EXPORT_DIR, TEST_IMAGE_PATH)


if __name__ == "__main__":
    main()


