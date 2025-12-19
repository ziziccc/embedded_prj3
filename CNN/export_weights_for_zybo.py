"""
Keras 모델 가중치를
- 레이어별 .npy
- 레이어별 C 헤더(.h, const float 배열)
- 공통 메타 헤더(classes.h, preprocessing.h, activations.h)
로 export.

주요 반영:
- 3-class softmax 모델을 가정 (출력 3 확인)
- 클래스 순서/이름 메타 생성 (대시보드/펌웨어와 동기화)
- Rescaling(1/255) 전처리 상수화
- softmax/argmax 유틸 제공

필요 시 아래 경로만 프로젝트 맞게 수정:
  MODEL_PATH  : Keras 모델 파일(.h5 / .keras)
  EXPORT_DIR  : 결과 저장 폴더
"""

import os
import numpy as np
from tensorflow import keras

# =========================
# 경로 설정
# =========================
MODEL_PATH = "C:\\cnn\\seat_multilabel_cnn.h5"  # 새 3-class softmax 모델 파일을 가리키도록 확인
EXPORT_DIR = "C:\\cnn\\export"

os.makedirs(EXPORT_DIR, exist_ok=True)

# =========================
# 클래스 메타 (순서 고정)
# =========================
CLASS_NAMES = ["PERSON", "BAG", "EMPTY"]  # softmax 출력 인덱스: 0,1,2
# 전처리 메타 (Keras의 Rescaling(1/255) 대응)
RESCALE_NUM = 1
RESCALE_DEN = 255

# =========================
# 헬퍼: C 배열 덤프
# =========================
def dump_c_array(fp, c_name: str, arr: np.ndarray):
    """
    arr -> 평탄화하여 const float 배열로 출력
    """
    flat = arr.flatten()
    fp.write(f"const float {c_name}[{flat.size}] = {{\n")
    for i, v in enumerate(flat):
        fp.write(f"  {float(v):.8e}f,")
        if (i + 1) % 8 == 0:
            fp.write("\n")
    if flat.size % 8 != 0:
        fp.write("\n")
    fp.write("};\n\n")

# =========================
# 공통 헤더들 생성
# =========================
def write_classes_header():
    path = os.path.join(EXPORT_DIR, "classes.h")
    with open(path, "w", encoding="utf-8") as fp:
        fp.write("#pragma once\n\n")
        fp.write("// === Class indices (softmax output order) ===\n")
        for idx, name in enumerate(CLASS_NAMES):
            fp.write(f"#define CLASS_{name} {idx}\n")
        fp.write(f"\n#define NUM_CLASSES {len(CLASS_NAMES)}\n\n")
        fp.write("static const char* CLASS_NAMES[NUM_CLASSES] = {\n")
        for name in CLASS_NAMES:
            fp.write(f'  "{name}",\n')
        fp.write("};\n")
    print(f"[INFO] Wrote {os.path.basename(path)}")

def write_preprocessing_header():
    path = os.path.join(EXPORT_DIR, "preprocessing.h")
    with open(path, "w", encoding="utf-8") as fp:
        fp.write("#pragma once\n\n")
        fp.write("// === Input preprocessing ===\n")
        fp.write("// Keras: Rescaling(1/255.0) -> x_norm = x_raw * (RESCALE_NUM / RESCALE_DEN)\n")
        fp.write(f"#define RESCALE_NUM {RESCALE_NUM}\n")
        fp.write(f"#define RESCALE_DEN {RESCALE_DEN}\n")
        fp.write("// 사용 예: float x = (float)px * ((float)RESCALE_NUM / (float)RESCALE_DEN);\n")
    print(f"[INFO] Wrote {os.path.basename(path)}")

def write_activations_header():
    path = os.path.join(EXPORT_DIR, "activations.h")
    with open(path, "w", encoding="utf-8") as fp:
        fp.write("#pragma once\n\n")
        fp.write("// === Softmax & Argmax reference implementations ===\n")
        fp.write("// 단정도(float) 기준의 참고용 코드. 플랫폼에 맞게 최적화/대체 가능.\n\n")
        fp.write("static inline int argmax(const float* x, int n) {\n")
        fp.write("  int mi = 0; float mv = x[0];\n")
        fp.write("  for (int i = 1; i < n; ++i) { if (x[i] > mv) { mv = x[i]; mi = i; } }\n")
        fp.write("  return mi;\n}\n\n")
        fp.write("static inline void softmax(const float* x, float* y, int n) {\n")
        fp.write("  // Max-trick for numerical stability\n")
        fp.write("  float m = x[0];\n")
        fp.write("  for (int i = 1; i < n; ++i) if (x[i] > m) m = x[i];\n")
        fp.write("  float s = 0.0f;\n")
        fp.write("  for (int i = 0; i < n; ++i) { y[i] = expf(x[i] - m); s += y[i]; }\n")
        fp.write("  float invs = 1.0f / s;\n")
        fp.write("  for (int i = 0; i < n; ++i) y[i] *= invs;\n}\n")
    print(f"[INFO] Wrote {os.path.basename(path)}")

# =========================
# 메인 로직
# =========================
def main():
    print(f"[INFO] Loading model: {MODEL_PATH}")
    model = keras.models.load_model(MODEL_PATH)
    model.summary()

    # 출력 차원 3 확인 (Dense 마지막 레이어 가정)
    last = model.layers[-1]
    try:
        out_units = last.units  # Dense일 경우
    except Exception:
        out_units = None

    if out_units != 3:
        print(f"[WARN] Model last layer units: {out_units} (expected 3).")
        print("       3-class softmax 모델이 아닌 것으로 보입니다. 헤더는 생성되지만 클래스 메타는 PERSON/BAG/EMPTY로 고정됩니다.")

    # 공통 메타 헤더 작성
    write_classes_header()
    write_preprocessing_header()
    write_activations_header()

    # 레이어별 가중치 export
    for layer in model.layers:
        weights = layer.get_weights()

        if len(weights) == 0:
            print(f"[INFO] Skip layer (no weights): {layer.name}")
            continue

        # Conv2D/Dense: 일반적으로 [kernel, bias]
        if len(weights) != 2:
            # BatchNorm 등은 파라미터가 2를 초과할 수 있음 → 스킵 or 수동 처리
            print(
                f"[WARN] Layer {layer.name}: expected 2 tensors (W,B), got {len(weights)}. Skipping automatic export."
            )
            continue

        W, B = weights
        W = np.asarray(W, dtype=np.float32)
        B = np.asarray(B, dtype=np.float32)

        # .npy 저장
        w_npy_path = os.path.join(EXPORT_DIR, f"{layer.name}_W.npy")
        b_npy_path = os.path.join(EXPORT_DIR, f"{layer.name}_B.npy")
        np.save(w_npy_path, W)
        np.save(b_npy_path, B)

        # C 헤더 저장
        header_path = os.path.join(EXPORT_DIR, f"{layer.name}_weights.h")
        macro = layer.name.upper()

        with open(header_path, "w", encoding="utf-8") as fp:
            fp.write("#pragma once\n\n")
            fp.write(f"// Layer    : {layer.name}\n")
            fp.write(f"// W shape  : {W.shape}\n")
            fp.write(f"// B shape  : {B.shape}\n")
            fp.write("// NOTE: Conv2D weight layout = (KH, KW, CIN, COUT)\n")
            fp.write("//       Dense weight layout  = (IN_DIM, OUT_DIM)\n\n")

            if W.ndim == 4:
                kh, kw, cin, cout = W.shape
                fp.write(f"#define {macro}_KERNEL_H {kh}\n")
                fp.write(f"#define {macro}_KERNEL_W {kw}\n")
                fp.write(f"#define {macro}_IN_CH    {cin}\n")
                fp.write(f"#define {macro}_OUT_CH   {cout}\n\n")
            elif W.ndim == 2:
                in_dim, out_dim = W.shape
                fp.write(f"#define {macro}_IN_DIM  {in_dim}\n")
                fp.write(f"#define {macro}_OUT_DIM {out_dim}\n\n")
            else:
                fp.write("// Unusual ndim; verify loader implementation.\n\n")

            dump_c_array(fp, f"{macro}_WEIGHTS", W)
            dump_c_array(fp, f"{macro}_BIASES", B)

        print(f"[INFO] Exported: {layer.name}  W{W.shape}, B{B.shape}")

    print(f"\n[INFO] Export complete. Output dir: {EXPORT_DIR}")

if __name__ == "__main__":
    main()
