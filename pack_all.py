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
PROJECTS_DIR = os.path.join(WORKSPACE, "projects")
PACK_ALL_DIR = os.path.join(WORKSPACE, "pack_all")
APPS_DIR = os.path.join(PACK_ALL_DIR, "apps")

EXTRA_PROJECTS = ["app_launcher", "app_settings"]
MAX_WORKERS = 1

lock = threading.Lock()
failed_projects = []

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

def process_project(proj_name, force=False, verbose=False):
    proj_path = os.path.join(PROJECTS_DIR, proj_name)

    if not os.path.isdir(proj_path):
        log_error(f"项目不存在: {proj_name}")
        return False

    log(f"[START] {proj_name}")

    try:
        current_hash = calc_project_hash(proj_path)
        saved_hash = get_saved_hash(proj_path)

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

            if not run_cmd(["maixcdk", "release", "-p", "maixcam"], proj_path, verbose):
                return False

            save_hash(proj_path, current_hash)

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
        # 目标目录名也使用 dist/pack 下“唯一目录名”，避免再套一层项目名。
        dst_dir = os.path.join(APPS_DIR, actual_pack_dir)

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


def prepare_apps_dir():
    if os.path.exists(APPS_DIR):
        shutil.rmtree(APPS_DIR)
    os.makedirs(APPS_DIR, exist_ok=True)


def analyze_projects(projects, force):
    cached = []
    need_build = []

    for proj in projects:
        proj_path = os.path.join(PROJECTS_DIR, proj)
        current_hash = calc_project_hash(proj_path)
        saved_hash = get_saved_hash(proj_path)

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


# ========= 新增：生成 app.info =========
def generate_app_info(apps_dir):
    app_info_content = "[basic]\nversion=1\n\n"

    high_priority_apps = ["app_store", "settings"]
    apps_info = {}

    for name in os.listdir(apps_dir):
        app_dir = os.path.join(apps_dir, name)
        if not os.path.isdir(app_dir):
            continue

        app_yaml_path = os.path.join(app_dir, "app.yaml")
        if not os.path.exists(app_yaml_path):
            continue

        with open(app_yaml_path, "r") as f:
            app_info = yaml.safe_load(f)

        if app_info["id"] == "launcher":
            continue

        s = f'[{app_info["id"]}]\n'
        valid_keys = ["name", "version", "icon", "author", "desc"]

        for k, v in app_info.items():
            if any(k.startswith(vk) for vk in valid_keys):
                s += f"{k}={v}\n"

        if "main.py" in os.listdir(app_dir):
            exec_path = "main.py"
        else:
            exec_path = app_info["id"]

        s += f"exec={exec_path}\n\n"

        apps_info[app_info["id"]] = s

    # 高优先级
    for id in high_priority_apps:
        if id in apps_info:
            app_info_content += apps_info[id]

    # 普通排序
    for id in sorted(apps_info.keys()):
        if id in high_priority_apps:
            continue
        app_info_content += apps_info[id]

    app_info_path = os.path.join(apps_dir, "app.info")
    with open(app_info_path, "w", encoding="utf-8") as f:
        f.write(app_info_content)

    print(f"[INFO] app.info 已生成")


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
    parser = argparse.ArgumentParser()
    parser.add_argument("--force", action="store_true", help="强制重新打包")
    parser.add_argument("--zip", action="store_true", help="完成后压缩")
    args = parser.parse_args()

    prepare_apps_dir()
    projects = collect_projects()

    projects = analyze_projects(projects, args.force)

    print("=== 第一轮：并行打包 ===")
    run_parallel_with_delay(projects, args.force)

    if failed_projects:
        print("\n=== 第二轮：失败项目重试 ===")
        for proj in failed_projects:
            print(f"\n>>> 重试项目: {proj}")
            process_project(proj, force=True, verbose=True)

    # ✅ 生成 app.info
    generate_app_info(APPS_DIR)

    # ✅ 可选压缩
    if args.zip:
        make_zip()

    print("\n=== 完成 ===")


if __name__ == "__main__":
    main()