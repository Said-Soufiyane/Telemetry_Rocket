from pathlib import Path
from collections import Counter, defaultdict
import os
import hashlib

ROOT = Path(r"I:\Projects\Telemetry_Rocket")
MAX_TREE_DEPTH = 3
TOP_N_LARGEST = 25
TOP_N_DUPLICATES = 20

IGNORE_DIRS = {
    ".git", "__pycache__", ".vscode", ".idea", "node_modules",
    "dist", "build", ".vs", "Debug", "Release", ".pio"
}

MESSY_KEYWORDS = [
    "copy", "backup", "old", "temp", "test", "finalfinal",
    "new folder", "untitled", "draft", "ver", "version"
]

LIKELY_PROJECT_FILES = {
    ".ino", ".py", ".cpp", ".c", ".h", ".hpp", ".md",
    ".stl", ".step", ".f3d", ".csv", ".json", ".txt",
    ".kicad_pcb", ".kicad_sch", ".pcbdoc", ".schdoc",
    ".xlsx", ".pdf", ".log"
}

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp"}
VIDEO_EXTS = {".mp4", ".mov", ".avi", ".mkv"}


def human_size(num):
    for unit in ["B", "KB", "MB", "GB", "TB"]:
        if num < 1024:
            return f"{num:.1f} {unit}"
        num /= 1024
    return f"{num:.1f} PB"


def safe_stat_size(path):
    try:
        return path.stat().st_size
    except Exception:
        return 0


def file_hash(path, chunk_size=65536):
    h = hashlib.sha256()
    try:
        with open(path, "rb") as f:
            while True:
                chunk = f.read(chunk_size)
                if not chunk:
                    break
                h.update(chunk)
        return h.hexdigest()
    except Exception:
        return None


def print_tree(path, prefix="", depth=0, max_depth=3, lines=None):
    if lines is None:
        lines = []
    if depth > max_depth:
        return lines
    try:
        items = sorted(path.iterdir(), key=lambda p: (p.is_file(), p.name.lower()))
    except Exception:
        lines.append(prefix + "[unreadable]")
        return lines

    for i, item in enumerate(items):
        connector = "└── " if i == len(items) - 1 else "├── "
        lines.append(prefix + connector + item.name)

        if item.is_dir() and item.name not in IGNORE_DIRS:
            extension = "    " if i == len(items) - 1 else "│   "
            print_tree(item, prefix + extension, depth + 1, max_depth, lines)
    return lines


def choose_cover_images(images):
    scored = []
    for img in images:
        name = img.name.lower()
        score = 0
        if "cover" in name:
            score += 10
        if "final" in name:
            score += 7
        if "front" in name:
            score += 6
        if "assembled" in name:
            score += 6
        if "photo" in name:
            score += 5
        if "rocket" in name:
            score += 5
        if "telemetry" in name:
            score += 5
        if "pcb" in name:
            score += 4
        if "wiring" in name:
            score += 3
        score -= len(img.parts) * 0.1
        scored.append((score, img))
    scored.sort(key=lambda x: (-x[0], str(x[1]).lower()))
    return [p for _, p in scored[:3]]


if not ROOT.exists():
    print(f"Path does not exist: {ROOT}")
    raise SystemExit(1)

file_count = 0
dir_count = 0
ext_counter = Counter()
size_by_ext = defaultdict(int)
largest_files = []
messy_files = []
hash_groups = defaultdict(list)
images = []
videos = []

for current_root, dirs, files in os.walk(ROOT):
    dirs[:] = [d for d in dirs if d not in IGNORE_DIRS]
    dir_count += len(dirs)

    current_path = Path(current_root)

    for f in files:
        file_count += 1
        full_path = current_path / f

        ext = full_path.suffix.lower() if full_path.suffix else "[no extension]"
        ext_counter[ext] += 1

        size = safe_stat_size(full_path)
        size_by_ext[ext] += size
        largest_files.append((size, full_path))

        if full_path.suffix.lower() in IMAGE_EXTS:
            images.append(full_path)
        if full_path.suffix.lower() in VIDEO_EXTS:
            videos.append(full_path)

        lower_name = f.lower()
        if any(keyword in lower_name for keyword in MESSY_KEYWORDS):
            messy_files.append(full_path)

        if size <= 50 * 1024 * 1024:
            h = file_hash(full_path)
            if h:
                hash_groups[h].append(full_path)

largest_files.sort(reverse=True, key=lambda x: x[0])

duplicate_groups = [paths for paths in hash_groups.values() if len(paths) > 1]
duplicate_groups.sort(key=lambda g: (-len(g), -sum(safe_stat_size(p) for p in g)))

print("=" * 80)
print("TELEMETRY ROCKET REPO AUDIT")
print("=" * 80)
print(f"Root: {ROOT}")

print("\nFOLDER TREE (depth 3)")
print("-" * 80)
print(ROOT.name)
tree_lines = print_tree(ROOT, max_depth=MAX_TREE_DEPTH)
for line in tree_lines:
    print(line)

print("\nSUMMARY")
print("-" * 80)
print(f"Folders: {dir_count}")
print(f"Files:   {file_count}")

print("\nFILE TYPES")
print("-" * 80)
for ext, count in ext_counter.most_common():
    print(f"{ext:15} {count:6} files   {human_size(size_by_ext[ext])}")

print("\nLARGEST FILES")
print("-" * 80)
for size, path in largest_files[:TOP_N_LARGEST]:
    print(f"{human_size(size):>10}   {path}")

print("\nMESSY / REVIEW THESE")
print("-" * 80)
if messy_files:
    for path in messy_files[:100]:
        print(path)
else:
    print("No obviously messy file names found.")

print("\nPOSSIBLE DUPLICATES (same file content)")
print("-" * 80)
if duplicate_groups:
    shown = 0
    for group in duplicate_groups:
        print(f"\nDuplicate group ({len(group)} files):")
        for path in group[:10]:
            print(f"  {path}")
        if len(group) > 10:
            print("  ...")
        shown += 1
        if shown >= TOP_N_DUPLICATES:
            break
else:
    print("No duplicates found.")

print("\nIMAGE CANDIDATES")
print("-" * 80)
cover_candidates = choose_cover_images(images)
if cover_candidates:
    for img in cover_candidates:
        print(img.relative_to(ROOT))
else:
    print("No images found.")

print("\nTOP-LEVEL CONTENT")
print("-" * 80)
try:
    for item in sorted(ROOT.iterdir(), key=lambda p: (p.is_file(), p.name.lower())):
        if item.is_dir():
            file_total = sum(1 for x in item.rglob("*") if x.is_file())
            print(f"[DIR ] {item.name:35} {file_total:6} files")
        else:
            print(f"[FILE] {item.name:35} {human_size(safe_stat_size(item))}")
except Exception as e:
    print(f"Could not list top-level content: {e}")

report_path = ROOT / "repo_audit_report.txt"
with open(report_path, "w", encoding="utf-8") as f:
    f.write("Telemetry Rocket Repo Audit completed successfully.\n")
    f.write(f"Root: {ROOT}\n")
    f.write(f"Folders: {dir_count}\n")
    f.write(f"Files: {file_count}\n")

print(f"\nSaved short report to: {report_path}")