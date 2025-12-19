## CNN 모델 제작 코드

from sklearn.metrics import confusion_matrix, classification_report
import matplotlib.pyplot as plt
import os
import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

# =========================
# 설정
# =========================
IMG_SIZE = 32
BATCH_SIZE = 32
EPOCHS = 20

DATA_ROOT = "C:\\dataset"   # dataset 폴더 위치
TRAIN_DIR = os.path.join(DATA_ROOT, "train")
VAL_DIR = os.path.join(DATA_ROOT, "val")

MODEL_DIR = "C:\\cnn"
MODEL_PATH = os.path.join(MODEL_DIR, "seat_multilabel_cnn.h5")  # 경로는 그대로 사용
WEIGHT_NPY_PATH = os.path.join(MODEL_DIR, "seat_multilabel_weights.npy")
WEIGHT_H5_PATH  = os.path.join(MODEL_DIR, "seat_multilabel_weights_only.h5")
os.makedirs(MODEL_DIR, exist_ok=True)

# =========================
# 상태 인덱스 정의 (confusion matrix용)
# 0: PERSON, 1: BAG, 2: EMPTY
# =========================
CLASS_NAMES = ["PERSON", "BAG", "EMPTY"]
PERSON_IDX = 0
BAG_IDX    = 1
EMPTY_IDX  = 2

# =========================
# 모델 정의 (single-label: 3-class softmax)
# =========================
def create_softmax3_model(input_shape=(IMG_SIZE, IMG_SIZE, 1)):
    inputs = keras.Input(shape=input_shape)
    x = layers.Rescaling(1.0 / 255.0)(inputs)

    x = layers.Conv2D(16, (3, 3), padding="same", activation="relu")(x)
    x = layers.MaxPooling2D((2, 2))(x)

    x = layers.Conv2D(32, (3, 3), padding="same", activation="relu")(x)
    x = layers.MaxPooling2D((2, 2))(x)

    x = layers.Conv2D(64, (3, 3), padding="same", activation="relu")(x)
    x = layers.MaxPooling2D((2, 2))(x)

    x = layers.Flatten()(x)
    x = layers.Dense(64, activation="relu")(x)

    # 3개 출력: [PERSON, BAG, EMPTY]
    outputs = layers.Dense(3, activation="softmax")(x)
    return keras.Model(inputs, outputs, name="seat_softmax3_cnn")

# =========================
# 데이터 로딩 함수 (원-핫 3클래스)
# 폴더 구조:
#  root_dir/
#    person/
#    bag/
#    empty/
# =========================
VALID_EXT = (".png", ".jpg", ".jpeg", ".bmp", ".gif")

def load_images_from_folder_singlelabel(root_dir):
    """
    root_dir: train/ 또는 val/ 디렉토리
    return: X (N, H, W, 1), y (N, 3)  # 3-class one-hot
    """
    X, y = [], []

    class_names = ["person", "bag", "empty"]
    # 폴더명 순서와 CLASS_NAMES 인덱스가 매칭되도록 주의
    label_map = {
        "person": np.array([1.0, 0.0, 0.0], dtype="float32"),  # PERSON
        "bag":    np.array([0.0, 1.0, 0.0], dtype="float32"),  # BAG
        "empty":  np.array([0.0, 0.0, 1.0], dtype="float32"),  # EMPTY
    }

    for cls in class_names:
        folder = os.path.join(root_dir, cls)
        if not os.path.isdir(folder):
            print(f"[WARN] folder not found, skip: {folder}")
            continue

        for fname in os.listdir(folder):
            if not fname.lower().endswith(VALID_EXT):
                continue
            fpath = os.path.join(folder, fname)

            img = keras.utils.load_img(
                fpath, color_mode="grayscale", target_size=(IMG_SIZE, IMG_SIZE)
            )
            img_array = keras.utils.img_to_array(img)  # (H, W, 1)
            X.append(img_array)
            y.append(label_map[cls])

    X = np.array(X, dtype="float32")
    y = np.array(y, dtype="float32")
    print(f"[INFO] Loaded from {root_dir}: X.shape={X.shape}, y.shape={y.shape}")
    return X, y

# =========================
# 메인: 학습 + confusion matrix
# =========================
def main():
    # 1) 데이터 로드 (one-hot 3클래스)
    X_train, y_train = load_images_from_folder_singlelabel(TRAIN_DIR)
    X_val, y_val     = load_images_from_folder_singlelabel(VAL_DIR)

    # 2) 모델 생성
    model = create_softmax3_model()
    model.summary()

    # 3) 컴파일 (단일라벨 3클래스)
    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=1e-3),
        loss=keras.losses.CategoricalCrossentropy(),
        metrics=[keras.metrics.CategoricalAccuracy(name="acc")],
    )

    # 4) 학습
    history = model.fit(
        X_train, y_train,
        validation_data=(X_val, y_val),
        epochs=EPOCHS,
        batch_size=BATCH_SIZE,
        shuffle=True,
    )

    # 5) 저장
    model.save(MODEL_PATH)
    print(f"[INFO] Model saved to {MODEL_PATH}")

    weights = model.get_weights()
    np.save(WEIGHT_NPY_PATH, np.array(weights, dtype=object), allow_pickle=True)
    print(f"[INFO] Weights (numpy) saved to {WEIGHT_NPY_PATH}")

    model.save_weights(WEIGHT_H5_PATH)
    print(f"[INFO] Weights (h5) saved to {WEIGHT_H5_PATH}")

    # =========================
    # 6) Validation confusion matrix 계산
    # =========================
    y_val_pred = model.predict(X_val)              # shape (N, 3), softmax 확률
    y_true_cls = np.argmax(y_val, axis=1)          # 정답 클래스 인덱스
    y_pred_cls = np.argmax(y_val_pred, axis=1)     # 예측 클래스 인덱스

    labels = [PERSON_IDX, BAG_IDX, EMPTY_IDX]
    cm = confusion_matrix(y_true_cls, y_pred_cls, labels=labels)

    print("\n[CONFUSION MATRIX] (rows = true, cols = pred)")
    print("           PRED:  PERSON   BAG  EMPTY")
    for i, row in enumerate(cm):
        print(f"TRUE {CLASS_NAMES[i]:6s}: {row[0]:7d} {row[1]:5d} {row[2]:6d}")

    print("\n[CLASSIFICATION REPORT]")
    print(classification_report(
        y_true_cls,
        y_pred_cls,
        target_names=CLASS_NAMES,
        labels=labels,
        digits=4,
        zero_division=0,
    ))

    # =========================
    # 7) Confusion Matrix 플롯 저장
    # =========================
    def plot_confusion_matrix(cm, classes, normalize=False, filename=None):
        if normalize:
            cm_sum = cm.sum(axis=1, keepdims=True)
            cm_sum[cm_sum == 0] = 1
            cm_to_show = cm.astype("float") / cm_sum
        else:
            cm_to_show = cm

        fig, ax = plt.subplots(figsize=(5, 4))
        im = ax.imshow(cm_to_show, interpolation="nearest")

        ax.set_xticks(range(len(classes)))
        ax.set_yticks(range(len(classes)))
        ax.set_xticklabels(classes, rotation=45, ha="right")
        ax.set_yticklabels(classes)
        ax.set_xlabel("Predicted label")
        ax.set_ylabel("True label")
        ax.set_title("Confusion Matrix")

        fmt = ".2f" if normalize else "d"
        thresh = cm_to_show.max() / 2.0 if cm_to_show.size > 0 else 0

        for i in range(cm_to_show.shape[0]):
            for j in range(cm_to_show.shape[1]):
                value = cm_to_show[i, j]
                ax.text(
                    j, i, format(value, fmt),
                    ha="center", va="center",
                    color="white" if value > thresh else "black",
                )

        fig.tight_layout()
        if filename is not None:
            fig.savefig(filename, dpi=300, bbox_inches="tight")
            print(f"[INFO] Confusion matrix figure saved to {filename}")
        else:
            plt.show()

    cm_count_path = os.path.join(MODEL_DIR, "cm_counts.png")
    plot_confusion_matrix(cm, classes=CLASS_NAMES, normalize=False, filename=cm_count_path)

    cm_norm_path = os.path.join(MODEL_DIR, "cm_normalized.png")
    plot_confusion_matrix(cm, classes=CLASS_NAMES, normalize=True, filename=cm_norm_path)

if __name__ == "__main__":
    main()

