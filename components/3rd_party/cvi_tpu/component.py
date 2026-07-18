
def add_file_downloads(confs : dict) -> list:
    '''
        @param confs kconfig vars, dict type
        @return list type, items is dict type
    '''
    import os, sys
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools", "cmake"))
    from maix_arch_util import get_maix_arch, toolchain_is_musl, toolchain_is_glibc

    if (not confs.get("PLATFORM_MAIXCAM", None)) or confs.get("CONFIG_COMPONENTS_COMPILE_FROM_SOURCE", None):
        return []
    arch = get_maix_arch(confs)
    if arch == "arm64":
        print("[cvi_tpu] PLATFORM_MAIXCAM arch=arm64: use MaixArm64Lib/lib/arm64 (no remote tarball yet)")
        return []
    if not toolchain_is_musl(confs) and not toolchain_is_glibc(confs):
        return []
    url = "https://github.com/sipeed/MaixCDK/releases/download/v0.0.0/cvi_tpu_lib_v4.1.0-23_2024.8.7.tar.xz"
    sha256sum = "15d88abdbc368690de0304240e3a859aec56b050283cc218803099d56be5251a"
    filename = "cvi_tpu_lib_v4.1.0-23_2024.8.7.tar.xz"
    path = "cvi_tpu"
    check_file = 'cvi_tpu_lib_v4.1.0-23_2024.8.7'
    sites = [
        'https://github.com/sipeed/MaixCDK/releases/tag/v0.0.0'
    ]

    return [
        {
            'url': f'{url}',
            'urls': [],
            'sites': sites,
            'sha256sum': sha256sum,
            'filename': filename,
            'path': path,
            'check_files': [
                check_file
            ]
        }
    ]
