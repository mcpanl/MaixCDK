
def add_file_downloads(confs : dict) -> list:
    '''
        @param confs kconfig vars, dict type
        @return list type, items is dict type
    '''
    import os, sys
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools", "cmake"))
    from maix_arch_util import get_maix_arch, toolchain_is_musl

    if (not confs.get("PLATFORM_MAIXCAM", None)) or confs.get("CONFIG_COMPONENTS_COMPILE_FROM_SOURCE", None) or confs.get("CONFIG_PYTHON3_COMPILE_FROM_SOURCE", None):
        version = f"{confs['CONFIG_PYTHON_VERSION_MAJOR']}.{confs['CONFIG_PYTHON_VERSION_MINOR']}.{confs['CONFIG_PYTHON_VERSION_PATCH']}"
        # Kconfig 0.0.0 means "auto" (see python3/CMakeLists.txt). CMake maps MaixCAM family to 3.11.6; mirror that here
        # so add_file_downloads still lists Python+zlib+openssl when compiling from source — otherwise zlib path is missing.
        if version == "0.0.0":
            if confs.get("PLATFORM_MAIXCAM") or confs.get("PLATFORM_MAIXCAM2") or confs.get("PLATFORM_RK3566"):
                version = "3.11.6"
            else:
                return []
        if version == "3.11.6":
            # Use .tgz (gzip) not .tar.xz: many CN mirrors ship a recompressed .xz with a different SHA than
            # python.org's canonical 92e14b22… xz; .tgz matches python.org / huaweicloud / npmmirror (c049bf31…).
            sha256sum = "c049bf317e877cbf9fce8c3af902436774ecef5249a29d10984ca3a37f7f4736"
            url = "https://mirrors.huaweicloud.com/python/3.11.6/Python-3.11.6.tgz"
            py_urls = [
                "https://registry.npmmirror.com/-/binary/python/3.11.6/Python-3.11.6.tgz",
                "https://www.python.org/ftp/python/3.11.6/Python-3.11.6.tgz",
            ]
        else:
            raise Exception(f"version {version} not support")
        sites = ["https://github.com/sipeed/MaixCDK/releases/tag/v0.0.0"]
        filename = f"Python-{version}.tgz"
        path = f"python_srcs"
        check_file = f'Python-{version}'
        rename = {}

        return [
            {
                'url': url,
                'urls': py_urls,
                'sites': sites,
                'sha256sum': sha256sum,
                'filename': filename,
                'path': path,
                'check_files': [
                    check_file
                ],
                'rename': rename
            },
            {
                'url': 'https://zlib.net/zlib-1.3.tar.xz',
                'urls': [
                    'https://github.com/madler/zlib/releases/download/v1.3/zlib-1.3.tar.xz',
                ],
                'sites': ['https://zlib.net/'],
                'sha256sum': '8a9ba2898e1d0d774eca6ba5b4627a11e5588ba85c8851336eb38de4683050a7',
                'filename': 'zlib-1.3.tar.xz',
                'path': 'zlib'
            },
            {
                'url': 'https://www.openssl.org/source/openssl-3.0.12.tar.gz',
                'urls': [
                    'https://github.com/openssl/openssl/releases/download/openssl-3.0.12/openssl-3.0.12.tar.gz',
                ],
                'sites': ['https://www.openssl.org/source/'],
                'sha256sum': 'f93c9e8edde5e9166119de31755fc87b4aa34863662f67ddfcba14d0b6b69b61',
                'filename': 'openssl-3.0.12.tar.gz',
                'path': 'openssl'
            }
        ]
    arch = get_maix_arch(confs)
    if arch == "arm64":
        print("[python3] PLATFORM_MAIXCAM arch=arm64: no prebuilt package URL yet, skip download")
        return []
    if not toolchain_is_musl(confs):
        return []
    version = "3.11.6"
    url = f"https://github.com/sipeed/MaixCDK/releases/download/v0.0.0/python3_lib_maixcam_musl_3.11.6.tar.xz"
    if version == "3.11.6":
        sha256sum = "92e14b22e708612c6e280931cc247b4266da9a6bac8459edf25bfb4cebcbac66"
    else:
        raise Exception(f"version {version} not support")
    sites = ["https://github.com/sipeed/MaixCDK/releases/tag/v0.0.0"]
    filename = f"python3_lib_maixcam_musl_3.11.6.tar.xz"
    path = f"python3"
    check_file = f'python3_lib_maixcam_musl_3.11.6'
    rename = {}

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
            ],
            'rename': rename
        }
    ]

