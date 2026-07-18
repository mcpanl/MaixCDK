import sys, re, os
import shutil
import yaml
import json


# current dir path
cur_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.append(cur_dir)
sys.path.append(os.path.join(cur_dir, ".."))
dl_dir = os.path.abspath(os.path.join(cur_dir, "..", "..", "dl"))
sdk_path = os.path.abspath(os.path.join(cur_dir, "..", ".."))
repo_root = os.path.abspath(os.path.join(sdk_path, ".."))

from file_downloader import Downloader, check_sha256sum, unzip_file, download_extract_files

VALID_ARCHES = ("riscv64", "arm64", "native")

def check_toolchain_info(info, board_name):
    # Local/external toolchains may omit download fields
    path_base = info.get("path_base", "dl_extracted")
    if path_base in ("repo_root", "sdk", "absolute") or not info.get("url"):
        keys = ["bin_path", "prefix"]
    else:
        keys = ["url", "sha256sum", "path", "bin_path", "prefix"]
    for key in keys:
        if key not in info or info[key] is None:
            # allow empty prefix only for host native
            if key == "prefix" and info.get("prefix") == "":
                continue
            if key == "bin_path" and not info.get("bin_path"):
                continue
            if key not in info:
                print("Error: %s not found in toolchain info of board %s.yaml" % (key, board_name))
                return

def infer_arch_from_toolchain(info):
    if info.get("arch"):
        return info["arch"]
    prefix = (info.get("prefix") or "").lower()
    bin_path = (info.get("bin_path") or "").lower()
    blob = prefix + " " + bin_path
    if "aarch64" in blob or "arm64" in blob:
        return "arm64"
    if "riscv64" in blob or "riscv" in blob:
        return "riscv64"
    return "native"

def select_multi_toolchain(board_name, toolchain_info, select_id, select_arch=None):
    if type(toolchain_info) is not list:
        if select_arch and infer_arch_from_toolchain(toolchain_info) != select_arch:
            print("Error: toolchain arch {} does not match requested --arch {}".format(
                infer_arch_from_toolchain(toolchain_info), select_arch))
            sys.exit(1)
        return toolchain_info
    if len(toolchain_info) == 1:
        info = toolchain_info[0]
        if select_arch and infer_arch_from_toolchain(info) != select_arch:
            print("Error: only toolchain arch {} available, requested --arch {}".format(
                infer_arch_from_toolchain(info), select_arch))
            sys.exit(1)
        return info

    # Prefer explicit toolchain id
    if select_id:
        for info in toolchain_info:
            if not "id" in info:
                print("Error: id not found in toolchain info of board %s.yaml" % board_name)
                sys.exit(1)
            if info['id'] == select_id:
                if select_arch and infer_arch_from_toolchain(info) != select_arch:
                    print("Error: toolchain id {} is arch {}, requested --arch {}".format(
                        select_id, infer_arch_from_toolchain(info), select_arch))
                    sys.exit(1)
                return info
        print("Error: toolchain id {} not found for platform {}".format(select_id, board_name))
        sys.exit(1)

    # Filter by arch if provided
    candidates = toolchain_info
    if select_arch:
        candidates = [i for i in toolchain_info if infer_arch_from_toolchain(i) == select_arch]
        if not candidates:
            print("Error: no toolchain with arch {} for platform {}".format(select_arch, board_name))
            print("Available:")
            for info in toolchain_info:
                print("  - {} (arch={})".format(info.get("id", "?"), infer_arch_from_toolchain(info)))
            sys.exit(1)
        if len(candidates) == 1:
            return candidates[0]

    print("This platform has multiple toolchains, please select one\n")
    default_idx = None
    for i, info in enumerate(candidates):
        is_default = info.get("default", False)
        idx_str = str(i + 1)
        arch = infer_arch_from_toolchain(info)
        print("\33[32m{}: {}\33[33m{}\33[0m\n{}{} [arch={}]\n".format(
            idx_str, info['id'], " (default)" if is_default else "",
            " " * (len(idx_str) + 2), info.get('name', ''), arch))
        if is_default:
            default_idx = i + 1
    # If filtering by arch left multiple and none default, pick first marked default among all or first
    if default_idx is None and select_arch:
        for i, info in enumerate(candidates):
            if info.get("default", False):
                default_idx = i + 1
                break
        if default_idx is None:
            default_idx = 1
    while True:
        select_idx = input("\nInput number to select toolchain, or Ctrl+C to cancel: ").strip()
        if default_idx is not None and select_idx == "":
            select_idx = default_idx
            break
        if not select_idx.isdigit():
            print("Error: please input number")
            continue
        select_idx = int(select_idx)
        if select_idx < 1 or select_idx > len(candidates):
            print("Error: please input number in range [1, {}]".format(len(candidates)))
            continue
        break
    return candidates[select_idx-1]

def resolve_toolchain_bin_path(toolchain_info):
    bin_path = toolchain_info.get("bin_path") or ""
    path_base = toolchain_info.get("path_base", "dl_extracted")
    if not bin_path:
        prefix = toolchain_info.get("prefix") or ""
        if prefix:
            gcc = prefix + "gcc"
            found = shutil.which(gcc)
            if found:
                return os.path.dirname(found)
            print("Error: cross compiler %r not found in PATH" % (gcc,))
            sys.exit(1)
        return None

    if os.path.isabs(bin_path) or path_base == "absolute":
        resolved = bin_path
    elif path_base == "repo_root":
        resolved = os.path.join(repo_root, bin_path)
    elif path_base == "sdk":
        resolved = os.path.join(sdk_path, bin_path)
    else:
        # default: under dl/extracted
        resolved = os.path.join(dl_dir, "extracted", bin_path)

    resolved = os.path.abspath(resolved)
    if not os.path.isdir(resolved):
        # For downloadable toolchains, directory may appear after extract; only hard-fail for local/external
        if path_base in ("repo_root", "sdk", "absolute") or not toolchain_info.get("url"):
            print("Error: toolchain bin_path not found: {}".format(resolved))
            print("  Hint: for MaixCAM arm64, run scripts/sync_maixcam_arm64_sdk.sh --toolchain-only")
            sys.exit(1)
    return resolved

def save_pkgs_info(sdk_path, files_info):
    info_path = os.path.join(sdk_path, "dl", "pkgs_info.json")
    count = 0
    for name, files in files_info.items():
        if len(files) > 0:
            count += len(files)
            for i, item in enumerate(files):
                item["pkg_path"] = os.path.join(sdk_path, "dl", "pkgs", item["path"], item["filename"])
    print("\n-------------------------------------------------------------------")
    print("-- All {} files info need to be downloaded saved to\n   {}".format(count, info_path))
    print("-------------------------------------------------------------------\n")
    os.makedirs(os.path.dirname(info_path), exist_ok=True)
    with open(info_path, "w") as f:
        json.dump(files_info, f, indent=4)

def main(board_name, boards_dir, out_cmake, toolchain_id = None, arch = None):

    # parse mk file, find PLATFORM_.*?=y to get board name
    if not board_name:
        print("Error: Platform name not set")
        sys.exit(1)

    if arch and arch not in VALID_ARCHES:
        print("Error: invalid arch {!r}, expected one of {}".format(arch, VALID_ARCHES))
        sys.exit(1)

    # parse boards_dir/{board_name}.yaml to get toolchain info
    toolchain_info = None
    board_yaml = os.path.join(boards_dir, board_name + '.yaml')
    with open(board_yaml, 'r') as f:
        board_info = yaml.safe_load(f)
        toolchain_info = board_info['toolchain']
    toolchain_info = select_multi_toolchain(board_name, toolchain_info, toolchain_id, arch)

    if not toolchain_info:
        print("Error: toolchain info not found in %s, please check yaml file of board" % board_yaml)
        sys.exit(1)

    check_toolchain_info(toolchain_info, board_name)

    maix_arch = infer_arch_from_toolchain(toolchain_info)
    if arch and maix_arch != arch:
        print("Error: resolved toolchain arch {} != requested {}".format(maix_arch, arch))
        sys.exit(1)

    toolchain_bin_path = resolve_toolchain_bin_path(toolchain_info)

    # generate cmake file, set CONFIG_TOOLCHAIN_PATH and CONFIG_TOOLCHAIN_PREFIX + ARCH
    os.makedirs(os.path.dirname(out_cmake), exist_ok=True)
    with open(out_cmake, 'w') as f:
        f.write('set(CONFIG_TOOLCHAIN_PATH "{}")\n'.format(toolchain_bin_path if toolchain_bin_path else ""))
        f.write('set(CONFIG_TOOLCHAIN_PREFIX "{}")\n'.format(toolchain_info['prefix'] if toolchain_info.get('prefix') else ""))
        f.write('set(MAIX_ARCH "{}")\n'.format(maix_arch))
        if maix_arch == "riscv64":
            f.write('set(CONFIG_ARCH_RISCV64 1)\n')
            f.write('set(CONFIG_ARCH_ARM64 0)\n')
        elif maix_arch == "arm64":
            f.write('set(CONFIG_ARCH_RISCV64 0)\n')
            f.write('set(CONFIG_ARCH_ARM64 1)\n')
        else:
            f.write('set(CONFIG_ARCH_RISCV64 0)\n')
            f.write('set(CONFIG_ARCH_ARM64 0)\n')
        libc = toolchain_info.get("libc") or ""
        if libc:
            f.write('set(MAIX_LIBC "{}")\n'.format(libc))

    # stash resolved fields for callers
    toolchain_info = dict(toolchain_info)
    toolchain_info["arch"] = maix_arch
    toolchain_info["resolved_bin_path"] = toolchain_bin_path

    print("-- Toolchain for platform %s arch %s is ready" % (board_name, maix_arch))
    return toolchain_info
