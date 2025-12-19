## train rate에 맞춰 train 데이터 valid 데이터 분할 코드


import os
import random
import shutil

# =========================================
# 설정 부분
# =========================================

# 원본 데이터가 클래스별로 들어있는 루트
# 예: dataset_raw/person, dataset_raw/bag, dataset_raw/empty
SRC_ROOT = "C:\\dataset"

# train/val 구조로 만들 대상 루트
DST_ROOT = "C:\\dataset"

# train 비율 (나머지는 val)
TRAIN_RATIO = 0.8

# 파일을 복사할지, 이동할지 선택
# True  -> copy (원본 유지)
# False -> move (원본에서 잘라내서 이동)
USE_COPY = True

VALID_EXT = (".jpg", ".jpeg", ".png", ".bmp")


# =========================================
# 유틸 함수
# =========================================
def ensure_dir(path: str):
    os.makedirs(path, exist_ok=True)


def main():
    # 클래스 이름 = SRC_ROOT 아래의 서브폴더 이름들
    class_names = [
        d for d in os.listdir(SRC_ROOT)
        if os.path.isdir(os.path.join(SRC_ROOT, d))
    ]

    if not class_names:
        print(f"[ERROR] {SRC_ROOT} 아래에 폴더(클래스)가 없습니다.")
        return

    print("[INFO] 클래스 목록:", class_names)

    # dst/train, dst/val 폴더 생성
    train_root = os.path.join(DST_ROOT, "train")
    val_root = os.path.join(DST_ROOT, "val")
    ensure_dir(train_root)
    ensure_dir(val_root)

    total_train = 0
    total_val = 0

    for cls in class_names:
        src_dir = os.path.join(SRC_ROOT, cls)

        # 이 클래스의 이미지 파일 목록
        files = [
            f for f in os.listdir(src_dir)
            if f.lower().endswith(VALID_EXT)
        ]

        if not files:
            print(f"[WARN] 클래스 '{cls}'에 이미지가 없습니다. 스킵.")
            continue

        random.shuffle(files)

        split_idx = int(len(files) * TRAIN_RATIO)
        train_files = files[:split_idx]
        val_files = files[split_idx:]

        # 목적지 폴더
        dst_train_cls = os.path.join(train_root, cls)
        dst_val_cls = os.path.join(val_root, cls)
        ensure_dir(dst_train_cls)
        ensure_dir(dst_val_cls)

        # 파일 복사/이동
        for fname in train_files:
            src_path = os.path.join(src_dir, fname)
            dst_path = os.path.join(dst_train_cls, fname)
            if USE_COPY:
                shutil.copy2(src_path, dst_path)
            else:
                shutil.move(src_path, dst_path)

        for fname in val_files:
            src_path = os.path.join(src_dir, fname)
            dst_path = os.path.join(dst_val_cls, fname)
            if USE_COPY:
                shutil.copy2(src_path, dst_path)
            else:
                shutil.move(src_path, dst_path)

        total_train += len(train_files)
        total_val += len(val_files)

        print(f"[INFO] 클래스 '{cls}': train {len(train_files)}개, val {len(val_files)}개")

    print("\n[SUMMARY]")
    print(f"전체 train 이미지 수: {total_train}")
    print(f"전체 val   이미지 수: {total_val}")
    print(f"생성된 train 폴더: {train_root}")
    print(f"생성된 val   폴더: {val_root}")

    if USE_COPY:
        print("\n[NOTE] USE_COPY=True 이므로 원본(SRC_ROOT)은 그대로 남아 있습니다.")
    else:
        print("\n[NOTE] USE_COPY=False 이므로 원본(SRC_ROOT)에서 파일을 이동했습니다.")


if __name__ == "__main__":
    main()

