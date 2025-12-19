# plot.py
# ------------------------------------------------------------
# Load a Keras .h5 model (full model OR weights-only) and export a model diagram
# using tensorflow.keras.utils.plot_model().
#
# This script ALSO auto-fixes Graphviz PATH on Windows by detecting dot.exe.
#
# Usage:
#   python plot.py --model "C:\cnn\seat_multilabel_cnn.h5" --out "C:\cnn\seat_model.png" --show-shapes --show-layer-names --rankdir LR
#
# Requirements:
#   python -m pip install tensorflow pydot graphviz
#   (Graphviz program must exist; this script will add its bin to PATH if found.)
# ------------------------------------------------------------

import os
import sys
import argparse
import shutil


def ensure_graphviz_on_path():
    """
    Ensure Graphviz 'dot' executable is discoverable via PATH.
    - If dot is already found: do nothing.
    - Else: try common folders; if dot.exe exists, prepend its folder to PATH.
    """
    # 1) Already on PATH?
    if shutil.which("dot") is not None:
        return True

    userprofile = os.environ.get("USERPROFILE", "")
    onedrive = os.environ.get("OneDrive", os.path.join(userprofile, "OneDrive"))

    # 2) Candidates (edit the first one if your folder name/path is different)
    GRAPHVIZ_CANDIDATES = [
        "C:\\Graphviz-14.1.1-win64\\bin",                    # 당신이 C:\로 옮긴 경우(가장 유력)
        os.path.join(onedrive, "Graphviz-14.1.1-win64\\bin"),# OneDrive 아래에 있는 경우
        "C:\\Program Files\\Graphviz\\bin",                   # 설치형 기본 경로
        "C:\\Program Files (x86)\Graphviz\\bin",
        "C:\\Graphviz\\bin",
    ]

    for gv_bin in GRAPHVIZ_CANDIDATES:
        if not gv_bin:
            continue
        dot_exe = os.path.join(gv_bin, "dot.exe")
        if os.path.isfile(dot_exe):
            os.environ["PATH"] = gv_bin + os.pathsep + os.environ.get("PATH", "")
            return shutil.which("dot") is not None

    return False


def build_model_for_weights():
    """
    weights-only .h5(= model.save_weights로 저장)인 경우에만 사용.
    full model(.h5 from model.save)이면 이 함수는 필요 없음.

    아래는 질문에서 사용하던 softmax 3-class CNN 구조 예시.
    실제 weights-only 파일 구조가 다르면 동일하게 맞춰야 load_weights가 성공합니다.
    """
    from tensorflow import keras
    from tensorflow.keras import layers

    IMG_SIZE = 32

    inputs = keras.Input(shape=(IMG_SIZE, IMG_SIZE, 1))
    x = layers.Rescaling(1.0 / 255.0)(inputs)

    x = layers.Conv2D(16, (3, 3), padding="same", activation="relu")(x)
    x = layers.MaxPooling2D((2, 2))(x)

    x = layers.Conv2D(32, (3, 3), padding="same", activation="relu")(x)
    x = layers.MaxPooling2D((2, 2))(x)

    x = layers.Conv2D(64, (3, 3), padding="same", activation="relu")(x)
    x = layers.MaxPooling2D((2, 2))(x)

    x = layers.Flatten()(x)
    x = layers.Dense(64, activation="relu")(x)

    outputs = layers.Dense(3, activation="softmax")(x)
    model = keras.Model(inputs, outputs, name="seat_softmax3_cnn")
    return model


def main():
    parser = argparse.ArgumentParser(
        description="Load a Keras .h5 model and export a model architecture diagram (PNG/SVG/PDF) using plot_model()."
    )
    parser.add_argument("--model", required=True, help="Path to .h5 file (full model or weights-only).")
    parser.add_argument("--out", default="model.png", help="Output image path (e.g., model.png).")
    parser.add_argument("--show-shapes", action="store_true", help="Show tensor shapes in the diagram.")
    parser.add_argument("--show-layer-names", action="store_true", help="Show layer names in the diagram.")
    parser.add_argument("--rankdir", default="TB", choices=["TB", "LR"], help="TB=top-bottom, LR=left-right.")
    parser.add_argument("--dpi", type=int, default=200, help="Output DPI.")
    args = parser.parse_args()

    model_path = args.model
    out_path = args.out

    if not os.path.isfile(model_path):
        print(f"[ERROR] Model file not found: {model_path}")
        sys.exit(1)

    # --- Ensure Graphviz dot is available ---
    ok = ensure_graphviz_on_path()
    dot_path = shutil.which("dot")
    print(f"[INFO] dot found at: {dot_path}")
    if not ok or dot_path is None:
        print("[ERROR] Graphviz 'dot' not found. Fix by either:")
        print("  - Installing Graphviz, OR")
        print("  - Editing GRAPHVIZ_CANDIDATES in this script to your actual ...\\bin path.")
        sys.exit(4)

    # Import TF/Keras after PATH fix
    try:
        from tensorflow import keras
        from tensorflow.keras.utils import plot_model
    except Exception as e:
        print("[ERROR] Failed to import TensorFlow/Keras.")
        print("        Make sure tensorflow is installed.")
        print("        python -m pip install tensorflow")
        raise

    # 1) Try loading as a full Keras model (.h5 from model.save)
    model = None
    try:
        print(f"[INFO] Trying to load full model: {model_path}")
        model = keras.models.load_model(model_path, compile=False)
        print("[INFO] Full model loaded successfully.")
    except Exception as e:
        print("[WARN] load_model() failed. This may be a weights-only .h5 or a custom-layer model.")
        print(f"       Reason: {type(e).__name__}: {e}")

    # 2) If full model load failed, assume weights-only and attach weights to a recreated architecture
    if model is None:
        try:
            print("[INFO] Falling back to weights-only workflow.")
            model = build_model_for_weights()
            model.load_weights(model_path)
            print("[INFO] Weights loaded into recreated model architecture.")
        except Exception as e:
            print("[ERROR] Failed to load as weights-only, too.")
            print("        If this is a custom model, you may need custom_objects or the exact architecture code.")
            print(f"        Reason: {type(e).__name__}: {e}")
            sys.exit(2)

    # 3) Export diagram
    try:
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        plot_model(
            model,
            to_file=out_path,
            show_shapes=args.show_shapes,
            show_layer_names=args.show_layer_names,
            rankdir=args.rankdir,
            dpi=args.dpi,
        )
        print(f"[DONE] Model diagram saved to: {os.path.abspath(out_path)}")
    except Exception as e:
        print("[ERROR] plot_model() failed.")
        print("        Common causes:")
        print("        - pydot not installed: python -m pip install pydot")
        print("        - Graphviz dot not accessible (PATH issue)")
        print(f"        Reason: {type(e).__name__}: {e}")
        sys.exit(3)


if __name__ == "__main__":
    main()
