import os
import numpy as np
from tensorflow.keras.preprocessing.image import load_img, img_to_array, array_to_img
from PIL import Image, ImageFilter, ImageEnhance

# =========================
# 설정
# =========================
IMG_SIZE = 32          # 증강 후 크기, 원본 크기 그대로 쓰고 싶으면 IMG_SIZE = None
INPUT_DIR  = "C:\\dataset\\bag"   # 원본 이미지 폴더
OUTPUT_DIR = "C:\\dataset\\bag\\aug"   # 증강 이미지 저장 폴더

# 변환별 생성 개수
N_GEOM_PER_IMG  = 1   # 기하학적 변형만
N_COLOR_PER_IMG = 1   # 색/밝기/대비만
N_BLUR_PER_IMG  = 1   # 블러만
N_NOISE_PER_IMG = 1   # 노이즈만

os.makedirs(OUTPUT_DIR, exist_ok=True)
VALID_EXT = (".png", ".jpg", ".jpeg", ".bmp")


# =========================
# 1) 기하학적 변형만 (회전 + 평행이동)
# =========================
def geom_aug(pil: Image.Image) -> Image.Image:
    w, h = pil.size

    # 작은 각도 회전
    angle = np.random.uniform(-5, 5)  # -5~5도
    img = pil.rotate(angle, resample=Image.BILINEAR, expand=False)

    # 작은 평행이동 (픽셀 단위)
    max_shift_x = int(0.08 * w)
    max_shift_y = int(0.08 * h)
    shift_x = np.random.randint(-max_shift_x, max_shift_x + 1)
    shift_y = np.random.randint(-max_shift_y, max_shift_y + 1)

    # Affine transform: (1, 0, tx, 0, 1, ty)
    img = img.transform(
        (w, h),
        Image.AFFINE,
        (1, 0, shift_x, 0, 1, shift_y),
        resample=Image.BILINEAR,
        fillcolor=None  # 주변은 자동 보간
    )
    return img


# =========================
# 2) 색/밝기/대비만
# =========================
def color_aug(pil: Image.Image) -> Image.Image:
    img = pil

    # 채도
    if np.random.rand() < 0.9:
        sat_factor = np.random.uniform(0.8, 1.2)
        img = ImageEnhance.Color(img).enhance(sat_factor)

    # 밝기
    if np.random.rand() < 0.9:
        bright_factor = np.random.uniform(0.85, 1.15)
        img = ImageEnhance.Brightness(img).enhance(bright_factor)

    # 대비
    if np.random.rand() < 0.9:
        cont_factor = np.random.uniform(0.85, 1.15)
        img = ImageEnhance.Contrast(img).enhance(cont_factor)

    return img


# =========================
# 3) 블러만
# =========================
def blur_aug(pil: Image.Image) -> Image.Image:
    radius = np.random.uniform(0.3, 1.0)
    img = pil.filter(ImageFilter.GaussianBlur(radius=radius))
    return img


# =========================
# 4) 노이즈만 (가우시안)
# =========================
def noise_aug(pil: Image.Image) -> Image.Image:
    arr = np.array(pil).astype("float32")   # (H, W, 3)
    noise_std = np.random.uniform(3, 10)
    noise = np.random.normal(0, noise_std, arr.shape)
    arr = arr + noise
    arr = np.clip(arr, 0, 255)
    img = Image.fromarray(arr.astype("uint8"))
    return img


# =========================
# 메인 루프
# =========================
def main():
    for fname in os.listdir(INPUT_DIR):
        if not fname.lower().endswith(VALID_EXT):
            continue

        fpath = os.path.join(INPUT_DIR, fname)
        prefix = os.path.splitext(fname)[0]

        print(f"[INFO] Processing {fname} ...")

        # 원본 로드
        if IMG_SIZE is None:
            base_img = load_img(fpath, color_mode="rgb")
        else:
            base_img = load_img(fpath, color_mode="rgb", target_size=(IMG_SIZE, IMG_SIZE))

        # PIL.Image 객체로
        base_pil = array_to_img(img_to_array(base_img).astype("uint8"))

        # ----- 1) 기하학적 변형만 -----
        for i in range(N_GEOM_PER_IMG):
            out_img = geom_aug(base_pil)
            save_name = f"{prefix}_geom_{i:03d}.png"
            out_img.save(os.path.join(OUTPUT_DIR, save_name))

        # ----- 2) 색/밝기/대비만 -----
        for i in range(N_COLOR_PER_IMG):
            out_img = color_aug(base_pil)
            save_name = f"{prefix}_color_{i:03d}.png"
            out_img.save(os.path.join(OUTPUT_DIR, save_name))

        # ----- 3) 블러만 -----
        for i in range(N_BLUR_PER_IMG):
            out_img = blur_aug(base_pil)
            save_name = f"{prefix}_blur_{i:03d}.png"
            out_img.save(os.path.join(OUTPUT_DIR, save_name))

        # ----- 4) 노이즈만 -----
        for i in range(N_NOISE_PER_IMG):
            out_img = noise_aug(base_pil)
            save_name = f"{prefix}_noise_{i:03d}.png"
            out_img.save(os.path.join(OUTPUT_DIR, save_name))

        print(
            f"  -> geom:{N_GEOM_PER_IMG}, color:{N_COLOR_PER_IMG}, "
            f"blur:{N_BLUR_PER_IMG}, noise:{N_NOISE_PER_IMG} 생성 완료"
        )

    print(f"\n전체 증강 완료 -> {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
