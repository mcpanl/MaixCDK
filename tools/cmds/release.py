#
# @file from https://github.com/Neutree/c_cpp_project_framework
# @author neucrack
# @license MIT
#

import argparse
import os
import subprocess
import sys
from maixtool.app_release import pack

parser = argparse.ArgumentParser(prog="release", description="release program as package file", add_help=False)

############################### Add option here #############################
parser.add_argument("-p", "--platform", help="Select platforms, use maixcdk build --help to see supported platforms", default="")
parser.add_argument("--arch", help="CPU architecture (maixcam: riscv64|arm64)", default="")
parser.add_argument("--toolchain-id", help="toolchain id", default="")

#############################################################################

# use project_args created by SDK_PATH/tools/cmake/project.py, e.g. project_args.terminal

def get_main_sh_content(app_id):
    content = '''#!/bin/bash
export LD_LIBRARY_PATH=./dl_lib:$LD_LIBRARY_PATH
chmod +x ./{app_id}
./{app_id} $@

'''
    return content.format(app_id=app_id)

def main(vars):
    '''
        @vars: dict,
            "project_path": project_path,
            "project_id": project_id,
            "sdk_path": sdk_path,
            "build_type": build_type,
            "project_parser": project_parser,
            "project_args": project_args,
            "configs": configs,
            "build_path": build_path (optional),
    '''

    print("\n--------- rebuild start ------------")
    # rebuild program
    cmd = ["maixcdk", "build", "--release"]
    if vars["project_args"].platform:
        cmd.append("--platform")
        cmd.append(vars["project_args"].platform)
    arch = getattr(vars["project_args"], "arch", "") or vars["configs"].get("MAIX_ARCH", "")
    if arch and arch != "native":
        cmd.append("--arch")
        cmd.append(arch)
    toolchain_id = getattr(vars["project_args"], "toolchain_id", "") or vars["configs"].get("TOOLCHAIN_ID", "")
    if toolchain_id:
        cmd.append("--toolchain-id")
        cmd.append(toolchain_id)
    exit_code = subprocess.Popen(cmd).wait()
    print("--------- rebuild complete ------------\n")
    if exit_code != 0:
        print("[ERROR] rebuild exit with code:{}".format(exit_code))
        sys.exit(1)

    build_path = vars.get("build_path") or vars["configs"].get("BUILD_DIR")
    if not build_path:
        # fallback: resolve from platform/arch
        platform = vars["configs"].get("PLATFORM", "maixcam")
        maix_arch = vars["configs"].get("MAIX_ARCH", "riscv64")
        if platform == "maixcam" and maix_arch in ("riscv64", "arm64"):
            build_path = os.path.join(vars["project_path"], "build", "{}_{}".format(platform, maix_arch))
        else:
            build_path = os.path.join(vars["project_path"], "build")

    exec_path = os.path.join(build_path, vars["project_id"])
    dl_lib_dir = os.path.join(build_path, "dl_lib")
    files = {}
    if os.path.exists(dl_lib_dir):
        files[dl_lib_dir] = "dl_lib"
    zip_path = pack(vars["project_path"], exec_path, extra_files = files)
    print("-- release complete, file:{}".format(zip_path))


# args = parser.parse_args()
if __name__ == '__main__':
    main()
