#!/usr/bin/env python3
import os
import shutil
import subprocess
import threading
import hashlib
import argparse
import yaml
import stat
from concurrent.futures import ThreadPoolExecutor

WORKSPACE = os.path.expanduser("~/maix/MaixCDK")
# Prefer running from checkout if pack_all.py lives in MaixCDK
_here = os.path.abspath(os.path.dirname(__file__))
if os.path.basename(_here) == "MaixCDK" or os.path.exists(os.path.join(_here, "platforms", "maixcam.yaml")):
    WORKSPACE = _here

PROJECTS_DIR = os.path.join(WORKSPACE, "projects")
PACK_ALL_DIR = os.path.join(WORKSPACE, "pack_all")
# 仅向 maixcdk_apps 写入/清空；maixpy_apps 不参与打包覆盖
MAIXCDK_APPS_DIR = os.path.join(PACK_ALL_DIR, "maixcdk_apps")
MAIXPY_APPS_DIR = os.path.join(PACK_ALL_DIR, "maixpy_apps")
# 最终平铺汇总：maixpy_apps + maixcdk_apps 下的各 app 目录 + app.info
FLAT_APPS_DIR = os.path.join(PACK_ALL_DIR, "apps")

EXTRA_PROJECTS = ["app_launcher", "app_settings"]
MAX_WORKERS = 1
DEFAULT_PLATFORM = "maixcam"
DEFAULT_ARCH = "riscv64"

lock = threading.Lock()
failed_projects = []
# Set in main()
BUILD_ARCH = DEFAULT_ARCH
BUILD_PLATFORM = DEFAULT_PLATFORM

# ========= hash =========
def calc_project_hash(proj_path):
    sha = hashlib.sha256()

    for root, dirs, files in os.walk(proj_path):
        dirs[:] = [d for d in dirs if d not in ["build", "dist"]]

        for f in sorted(files):
            file_path = os.path.join(root, f)
            try:
                with open(file_path, "rb") as fp:
                    while chunk := fp.read(8192):
                        sha.update(chunk)
            except:
                pass

    return sha.hexdigest()


def get_saved_hash(proj_path):
    hash_file = os.path.join(proj_path, "dist", "pack", ".pack_hash")
    if os.path.exists(hash_file):
        return open(hash_file).read().strip()
    return None


def save_hash(proj_path, h):
    hash_file = os.path.join(proj_path, "dist", "pack", ".pack_hash")
    os.makedirs(os.path.dirname(hash_file), exist_ok=True)
    with open(hash_file, "w") as f:
        f.write(h)


def ensure_executable_permissions(app_dir, proj_name=None):
    """
    为 app_dir 下可能作为入口的二进制文件补充可执行权限。
    规则：
    1) 目录同名文件（必须满足用户期望，例如 k_nn_camera/k_nn_camera）
    2) app.yaml 里的 id 同名文件（兼容历史项目）
    3) 项目目录名同名文件（兜底）
    """
    if not os.path.isdir(app_dir):
        return

    app_dir_name = os.path.basename(app_dir)
    candidates = {app_dir_name}
    if proj_name:
        candidates.add(proj_name)

    app_yaml_path = os.path.join(app_dir, "app.yaml")
    if os.path.exists(app_yaml_path):
        try:
            with open(app_yaml_path, "r", encoding="utf-8") as f:
                app_info = yaml.safe_load(f) or {}
            app_id = app_info.get("id")
            if isinstance(app_id, str) and app_id:
                candidates.add(app_id)
        except Exception as e:
            log_error(f"{app_dir} 读取 app.yaml 失败，跳过 id 权限修复: {e}")

    for name in candidates:
        exec_path = os.path.join(app_dir, name)
        if os.path.isfile(exec_path):
            current_mode = os.stat(exec_path).st_mode
            new_mode = current_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
            if new_mode != current_mode:
                os.chmod(exec_path, new_mode)
            log(f"[CHMOD +x] {exec_path}")

def get_single_subdir_name(parent_dir):
    """
    返回 parent_dir 下“且仅有一个”的子目录名；否则返回 None。

    用于适配 dist/pack 输出目录名不固定的情况。
    """
    if not os.path.isdir(parent_dir):
        return None

    subdirs = []
    for name in os.listdir(parent_dir):
        full_path = os.path.join(parent_dir, name)
        if os.path.isdir(full_path):
            subdirs.append(name)

    if len(subdirs) == 1:
        return subdirs[0]
    return None


def log(msg):
    with lock:
        print(msg)


def log_error(msg):
    with lock:
        print(f"[ERROR] {msg}")


def run_cmd(cmd, cwd, verbose=False):
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        if result.returncode != 0:
            if verbose:
                log(f"\n===== {cwd} 执行失败 =====")
                print(result.stdout)
                print(result.stderr)
                log("===== END =====\n")
            else:
                log_error(f"{cwd} 执行失败:\n{result.stderr.strip()}")
            return False

        if verbose:
            log(f"\n===== {cwd} 执行成功 =====")
            print(result.stdout)
            log("===== END =====\n")

        return True

    except Exception as e:
        log_error(f"{cwd} 执行异常: {e}")
        return False


def clean_project(proj_path):
    try:
        for d in ["build", "dist"]:
            path = os.path.join(proj_path, d)
            if os.path.exists(path):
                shutil.rmtree(path)
    except Exception as e:
        log_error(f"{proj_path} 清理失败: {e}")
        return False
    return True

def elf_arch_ok(path, arch):
    """Return True if path is ELF matching arch, or non-ELF / missing."""
    try:
        with open(path, "rb") as f:
            if f.read(4) != b"\x7fELF":
                return True
            f.read(12)
            f.read(2)
            em = int.from_bytes(f.read(2), "little")
        expect = {"riscv64": 243, "arm64": 183}.get(arch)
        if expect is None:
            return True
        return em == expect
    except Exception:
        return True


def process_project(proj_name, force=False, verbose=False):
    proj_path = os.path.join(PROJECTS_DIR, proj_name)

    if not os.path.isdir(proj_path):
        log_error(f"项目不存在: {proj_name}")
        return False

    log(f"[START] {proj_name} ({BUILD_PLATFORM}/{BUILD_ARCH})")

    try:
        current_hash = calc_project_hash(proj_path)
        # Hash file is arch-specific to avoid cross-arch cache hits
        hash_file = os.path.join(proj_path, "dist", "pack", f".pack_hash_{BUILD_ARCH}")
        saved_hash = open(hash_file).read().strip() if os.path.exists(hash_file) else None

        need_build = force or (saved_hash != current_hash)

        dist_dir = os.path.join(proj_path, "dist")
        pack_root = os.path.join(dist_dir, "pack")

        # cache命中但 dist/pack 目录结构不符合“仅有一个实际目录” → 强制编译
        if not need_build:
            existing_pack_dir = get_single_subdir_name(pack_root)
            if not existing_pack_dir:
                log(f"[MISS DIST] {proj_name} dist/pack 目录不满足单目录约束，重新编译")
                need_build = True

        if not need_build:
            log(f"[CACHE HIT] {proj_name} 跳过编译，直接复制")
        else:
            if not clean_project(proj_path):
                return False

            cmd = ["maixcdk", "release", "-p", BUILD_PLATFORM, "--arch", BUILD_ARCH]
            if not run_cmd(cmd, proj_path, verbose):
                return False

            os.makedirs(os.path.dirname(hash_file), exist_ok=True)
            with open(hash_file, "w") as f:
                f.write(current_hash)

        # ========= 关键修复 =========
        # dist/pack 下的实际目录名不固定，且要求“有且仅有一个”
        actual_pack_dir = get_single_subdir_name(pack_root)
        if not actual_pack_dir:
            if os.path.isdir(pack_root):
                found = [
                    name for name in os.listdir(pack_root)
                    if os.path.isdir(os.path.join(pack_root, name))
                ]
                log_error(f"{proj_name} 未生成 dist/pack 下唯一目录：找到 {found}")
            else:
                log_error(f"{proj_name} 未生成 dist/pack（目录不存在）")
            return False

        src_dir = os.path.join(pack_root, actual_pack_dir)
        # Validate main binary ELF arch when present
        for cand in (actual_pack_dir, proj_name):
            bin_path = os.path.join(src_dir, cand)
            if os.path.isfile(bin_path) and not elf_arch_ok(bin_path, BUILD_ARCH):
                log_error(f"{proj_name} ELF 架构与 --arch {BUILD_ARCH} 不匹配: {bin_path}")
                return False

        # 目标目录名也使用 dist/pack 下“唯一目录名”，避免再套一层项目名。
        # Arch-isolated apps root: pack_all/maixcdk_apps_<arch>/
        apps_root = MAIXCDK_APPS_DIR
        if BUILD_ARCH in ("riscv64", "arm64"):
            apps_root = os.path.join(PACK_ALL_DIR, f"maixcdk_apps_{BUILD_ARCH}")
            os.makedirs(apps_root, exist_ok=True)
        dst_dir = os.path.join(apps_root, actual_pack_dir)

        if os.path.exists(dst_dir):
            shutil.rmtree(dst_dir)

        shutil.copytree(src_dir, dst_dir)
        ensure_executable_permissions(dst_dir, proj_name=proj_name)
        # ===========================

        log(f"[DONE] {proj_name}")
        return True

    except Exception as e:
        log_error(f"{proj_name} 处理异常: {e}")
        return False

def collect_projects():
    projects = []

    for name in os.listdir(PROJECTS_DIR):
        if name.startswith("k_"):
            projects.append(name)

    projects.extend(EXTRA_PROJECTS)
    return projects


def prepare_maixcdk_apps_dir():
    """每次打包只清空/重建当前 arch 的 maixcdk_apps，不改动 maixpy_apps。"""
    global MAIXCDK_APPS_DIR
    if BUILD_ARCH in ("riscv64", "arm64"):
        MAIXCDK_APPS_DIR = os.path.join(PACK_ALL_DIR, f"maixcdk_apps_{BUILD_ARCH}")
    if os.path.exists(MAIXCDK_APPS_DIR):
        shutil.rmtree(MAIXCDK_APPS_DIR)
    os.makedirs(MAIXCDK_APPS_DIR, exist_ok=True)
    # Keep legacy alias for default riscv64
    if BUILD_ARCH == "riscv64":
        legacy = os.path.join(PACK_ALL_DIR, "maixcdk_apps")
        if os.path.islink(legacy) or os.path.exists(legacy):
            try:
                if os.path.islink(legacy) or os.path.isfile(legacy):
                    os.remove(legacy)
                elif os.path.isdir(legacy) and legacy != MAIXCDK_APPS_DIR:
                    shutil.rmtree(legacy)
            except Exception:
                pass
        try:
            if not os.path.exists(legacy):
                os.symlink(os.path.basename(MAIXCDK_APPS_DIR), legacy)
        except Exception:
            pass


def analyze_projects(projects, force):
    cached = []
    need_build = []

    for proj in projects:
        proj_path = os.path.join(PROJECTS_DIR, proj)
        current_hash = calc_project_hash(proj_path)
        hash_file = os.path.join(proj_path, "dist", "pack", f".pack_hash_{BUILD_ARCH}")
        saved_hash = open(hash_file).read().strip() if os.path.exists(hash_file) else None

        if not force and saved_hash == current_hash:
            cached.append(proj)
        else:
            need_build.append(proj)

    print("\n=== 缓存分析 ===")
    print("使用缓存:")
    for p in cached:
        print(f"  ✓ {p}")

    print("\n需要打包:")
    for p in need_build:
        print(f"  → {p}")

    print("================\n")

    return need_build + cached


# ========= 生成 app.info（合并多个 app 根目录）=========
def _scan_apps_dir_to_sections(apps_dir):
    """
    扫描 apps_dir 下一层子目录中的 app.yaml，返回 { app_id: ini段落字符串 }。
    """
    sections = {}
    if not os.path.isdir(apps_dir):
        return sections

    for name in os.listdir(apps_dir):
        app_dir = os.path.join(apps_dir, name)
        if not os.path.isdir(app_dir):
            continue

        app_yaml_path = os.path.join(app_dir, "app.yaml")
        if not os.path.exists(app_yaml_path):
            continue

        try:
            with open(app_yaml_path, "r", encoding="utf-8") as f:
                app_info = yaml.safe_load(f)
        except Exception as e:
            log_error(f"{app_yaml_path} 读取失败，跳过: {e}")
            continue

        if not app_info or not isinstance(app_info, dict):
            continue

        app_id = app_info.get("id")
        if not isinstance(app_id, str) or not app_id:
            continue
        if app_id == "launcher":
            continue

        s = f"[{app_id}]\n"
        valid_keys = ["name", "version", "icon", "author", "desc"]

        for k, v in app_info.items():
            if any(k.startswith(vk) for vk in valid_keys):
                s += f"{k}={v}\n"

        if "main.py" in os.listdir(app_dir):
            exec_path = "main.py"
        else:
            exec_path = app_id

        s += f"exec={exec_path}\n\n"
        sections[app_id] = s

    return sections


def generate_app_info(pack_all_dir, *apps_roots):
    """
    合并 apps_roots 中各目录下的应用，写入 pack_all_dir/app.info。
    后传入的目录中相同 app id 会覆盖先传入的（默认 maixpy 在前、maixcdk 在后，原生包优先）。
    """
    app_info_content = "[basic]\nversion=1\n\n"
    high_priority_apps = ["app_store", "settings"]
    apps_info = {}

    for root in apps_roots:
        if root and os.path.isdir(root):
            apps_info.update(_scan_apps_dir_to_sections(root))

    for id in high_priority_apps:
        if id in apps_info:
            app_info_content += apps_info[id]

    for id in sorted(apps_info.keys()):
        if id in high_priority_apps:
            continue
        app_info_content += apps_info[id]

    app_info_path = os.path.join(pack_all_dir, "app.info")
    os.makedirs(pack_all_dir, exist_ok=True)
    with open(app_info_path, "w", encoding="utf-8") as f:
        f.write(app_info_content)

    print(f"[INFO] app.info 已生成: {app_info_path}")


def materialize_flat_apps_dir():
    """
    清空并重建 pack_all/apps，将 maixpy_apps、maixcdk_apps 中各一级子目录
    平铺复制到 apps/（同名以后复制的 maixcdk 为准），并复制 pack_all/app.info。
    """
    if os.path.exists(FLAT_APPS_DIR):
        shutil.rmtree(FLAT_APPS_DIR)
    os.makedirs(FLAT_APPS_DIR, exist_ok=True)

    def copy_app_subdirs(src_root):
        if not os.path.isdir(src_root):
            return
        for name in sorted(os.listdir(src_root)):
            src = os.path.join(src_root, name)
            if not os.path.isdir(src):
                continue
            dst = os.path.join(FLAT_APPS_DIR, name)
            if os.path.exists(dst):
                shutil.rmtree(dst)
            shutil.copytree(src, dst)

    copy_app_subdirs(MAIXPY_APPS_DIR)
    copy_app_subdirs(MAIXCDK_APPS_DIR)

    merged_info = os.path.join(PACK_ALL_DIR, "app.info")
    if os.path.isfile(merged_info):
        shutil.copy2(merged_info, os.path.join(FLAT_APPS_DIR, "app.info"))
        print(f"[INFO] 已平铺复制到: {FLAT_APPS_DIR}（含 app.info）")
    else:
        log_error(f"未找到 {merged_info}，跳过复制到 apps/")


def make_zip():
    zip_path = os.path.join(WORKSPACE, "pack_all")
    print("\n=== 开始压缩 ===")
    try:
        shutil.make_archive(zip_path, 'zip', PACK_ALL_DIR)
    except Exception as e:
        log_error(f"压缩失败: {e}")


def run_parallel_with_delay(projects, force):
    futures = []
    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
        for proj in projects:
            futures.append((proj, executor.submit(process_project, proj, force)))

        for proj, future in futures:
            try:
                success = future.result()
                if not success:
                    failed_projects.append(proj)
            except Exception as e:
                log_error(f"{proj} 线程异常: {e}")
                failed_projects.append(proj)


def main():
    global BUILD_ARCH, BUILD_PLATFORM, failed_projects
    parser = argparse.ArgumentParser()
    parser.add_argument("--force", action="store_true", help="强制重新打包")
    parser.add_argument("--zip", action="store_true", help="完成后压缩")
    parser.add_argument("-p", "--platform", default=DEFAULT_PLATFORM, help="平台，默认 maixcam")
    parser.add_argument("--arch", default=DEFAULT_ARCH, choices=["riscv64", "arm64"],
                        help="架构，默认 riscv64")
    args = parser.parse_args()

    BUILD_PLATFORM = args.platform
    BUILD_ARCH = args.arch
    failed_projects = []

    prepare_maixcdk_apps_dir()
    projects = collect_projects()

    projects = analyze_projects(projects, args.force)

    print(f"=== 第一轮：并行打包 platform={BUILD_PLATFORM} arch={BUILD_ARCH} ===")
    run_parallel_with_delay(projects, args.force)

    if failed_projects:
        print("\n=== 第二轮：失败项目重试 ===")
        for proj in list(failed_projects):
            print(f"\n>>> 重试项目: {proj}")
            if process_project(proj, force=True, verbose=True):
                failed_projects.remove(proj)

    # 合并 maixpy_apps + maixcdk_apps 生成顶层 app.info
    generate_app_info(PACK_ALL_DIR, MAIXPY_APPS_DIR, MAIXCDK_APPS_DIR)
    materialize_flat_apps_dir()

    # ✅ 可选压缩
    if args.zip:
        make_zip()

    print("\n=== 完成 ===")


if __name__ == "__main__":
    main()