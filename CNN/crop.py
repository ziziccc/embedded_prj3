import os
import cv2

# ======================
# 설정
# ======================
# Kaggle에서 받은 데이터셋 폴더 구조에 맞게 수정!
# 예시:
#   dataset_root/
#       images/
#       labels/
DATA_ROOT = "C:\\dataset\\archive (2)\\chair" # <- 여기를 실제 경로로 맞추기(사람 머리면 "C:\\dataset\\head")
IMG_DIR   = os.path.join(DATA_ROOT, "images")
LBL_DIR   = os.path.join(DATA_ROOT, "labels")

OUT_DIR   = "C:\\dataset\\archive (2)\\chair\\crop" # <- 여기를 실제 경로로 맞추기(사람 머리면 "C:\\dataset\\head\\crop")
os.makedirs(OUT_DIR, exist_ok=True)

# 가방 = 7 사람 머리 = 0 의자 = 12
BAG_CLASS_ID = 12

IMG_EXT = (".jpg", ".jpeg", ".png", ".bmp")

def yolo_to_xyxy(xc, yc, w, h, img_w, img_h):
    """YOLO normalized -> (x_min, y_min, x_max, y_max)"""
    x_c = xc * img_w
    y_c = yc * img_h
    bw  = w  * img_w
    bh  = h  * img_h

    x_min = int(x_c - bw / 2)
    x_max = int(x_c + bw / 2)
    y_min = int(y_c - bh / 2)
    y_max = int(y_c + bh / 2)

    x_min = max(0, x_min)
    y_min = max(0, y_min)
    x_max = min(img_w - 1, x_max)
    y_max = min(img_h - 1, y_max)

    return x_min, y_min, x_max, y_max


def crop_bags_from_image(img_path, label_path, save_prefix):
    print(f"\n[INFO] 이미지 처리 중: {img_path}")
    img = cv2.imread(img_path)
    if img is None:
        print(f"[WARN] 이미지 로드 실패 (None): {img_path}")
        return 0

    h, w = img.shape[:2]
    print(f"[DEBUG] 이미지 사이즈: w={w}, h={h}")

    if not os.path.exists(label_path):
        print(f"[WARN] 라벨 파일 없음: {label_path}")
        return 0

    with open(label_path, "r") as f:
        lines = f.readlines()

    if not lines:
        print(f"[WARN] 라벨 내용 없음: {label_path}")
        return 0

    print(f"[DEBUG] 라벨 줄 수: {len(lines)}")

    bag_count = 0
    for i, line in enumerate(lines):
        parts = line.strip().split()
        print(f"[DEBUG] line {i}: {parts}")

        if len(parts) < 5:
            print(f"[WARN] 형식 이상 (5개 미만): {line}")
            continue

        try:
            cls_id = int(float(parts[0]))
            xc = float(parts[1])
            yc = float(parts[2])
            bw = float(parts[3])
            bh = float(parts[4])
        except ValueError:
            print(f"[WARN] 숫자 파싱 실패: {line}")
            continue

        print(f"[DEBUG] cls_id={cls_id}, xc={xc:.4f}, yc={yc:.4f}, bw={bw:.4f}, bh={bh:.4f}")

        if cls_id != BAG_CLASS_ID:
            continue

        x_min, y_min, x_max, y_max = yolo_to_xyxy(xc, yc, bw, bh, w, h)
        print(f"[DEBUG] crop box: ({x_min}, {y_min}) ~ ({x_max}, {y_max})")

        if x_max <= x_min or y_max <= y_min:
            print("[WARN] 잘못된 박스 (면적 0): 스킵")
            continue

        crop = img[y_min:y_max, x_min:x_max]

        if crop.size == 0:
            print("[WARN] crop.size == 0 -> 스킵")
            continue

        out_name = f"{save_prefix}_bag_{bag_count:03d}.jpg"
        out_path = os.path.join(OUT_DIR, out_name)
        ok = cv2.imwrite(out_path, crop)
        print(f"[DEBUG] 저장 {out_path}, 성공={ok}")
        bag_count += 1

    print(f"[INFO] {img_path} 에서 가방 {bag_count}개 추출")
    return bag_count


def main():
    print("[INFO] IMG_DIR:", IMG_DIR)
    print("[INFO] LBL_DIR:", LBL_DIR)
    print("[INFO] OUT_DIR:", OUT_DIR)

    if not os.path.isdir(IMG_DIR):
        print("[ERROR] IMG_DIR가 폴더가 아님. 경로 다시 확인 필요.")
        return
    if not os.path.isdir(LBL_DIR):
        print("[ERROR] LBL_DIR가 폴더가 아님. 경로 다시 확인 필요.")
        return

    img_files = [f for f in os.listdir(IMG_DIR)
                 if f.lower().endswith(IMG_EXT)]

    print("[INFO] 이미지 파일 개수:", len(img_files))

    total_bags = 0
    for idx, fname in enumerate(img_files):
        img_path = os.path.join(IMG_DIR, fname)
        base, _ = os.path.splitext(fname)
        lbl_path = os.path.join(LBL_DIR, base + ".txt")

        print(f"\n[DEBUG] {idx}: img={img_path}")
        print(f"[DEBUG]      label={lbl_path}, exists={os.path.exists(lbl_path)}")

        bags = crop_bags_from_image(img_path, lbl_path, save_prefix=base)
        total_bags += bags

if __name__ == "__main__":
    main()
