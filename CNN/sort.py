import os
import re

FOLDER = "C:\\dataset\\bag" # 이름 정리할 폴더 경로

VALID_EXT = (".jpg", ".jpeg", ".png", ".bmp")

def extract_number(fname):
    nums = re.findall(r"\d+", fname)
    if not nums:
        return 0
    return int(nums[-1])

files = [f for f in os.listdir(FOLDER)
         if f.lower().endswith(VALID_EXT)]

files_sorted = sorted(files, key=extract_number)

# 자릿수 결정 (총 개수에 맞게)
n = len(files_sorted)
width = len(str(n))  # 예: 123개면 width=3 -> 001~123

print("총 파일 수:", n)
print("자릿수:", width)

for idx, old_name in enumerate(files_sorted, start=1):
    _, ext = os.path.splitext(old_name)
    new_name = f"{idx:0{width}d}{ext.lower()}"  # 001.jpg, 002.jpg ...

    old_path = os.path.join(FOLDER, old_name)
    new_path = os.path.join(FOLDER, new_name)

    print(f"{old_name} -> {new_name}")
    os.rename(old_path, new_path)

print("이름 정리 완료.")
