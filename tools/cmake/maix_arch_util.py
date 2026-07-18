"""Shared helpers for component.py download / arch selection."""

def get_maix_arch(confs: dict) -> str:
    arch = confs.get("MAIX_ARCH") or confs.get("CONFIG_MAIX_ARCH") or ""
    if arch:
        return str(arch)
    # Infer from known platforms
    if confs.get("PLATFORM_MAIXCAM2") or confs.get("PLATFORM_RK3566") or confs.get("PLATFORM_ZONHOR"):
        return "arm64"
    if confs.get("PLATFORM_MAIXCAM"):
        # legacy: toolchain path heuristic
        tc = str(confs.get("CONFIG_TOOLCHAIN_PATH", "") or "")
        if "aarch64" in tc or "arm64" in tc:
            return "arm64"
        return "riscv64"
    if confs.get("PLATFORM_LINUX"):
        return "native"
    return "native"


def is_maixcam_riscv64(confs: dict) -> bool:
    return bool(confs.get("PLATFORM_MAIXCAM")) and get_maix_arch(confs) == "riscv64"


def is_maixcam_arm64(confs: dict) -> bool:
    # zonhor is the MaixCAM arm64 glibc board alias
    if confs.get("PLATFORM_ZONHOR"):
        return True
    return bool(confs.get("PLATFORM_MAIXCAM")) and get_maix_arch(confs) == "arm64"


def toolchain_is_musl(confs: dict) -> bool:
    tc = str(confs.get("CONFIG_TOOLCHAIN_PATH", "") or "")
    libc = str(confs.get("MAIX_LIBC", "") or "")
    return "musl" in tc or libc == "musl"


def toolchain_is_glibc(confs: dict) -> bool:
    tc = str(confs.get("CONFIG_TOOLCHAIN_PATH", "") or "")
    libc = str(confs.get("MAIX_LIBC", "") or "")
    return "glibc" in tc or "linux-gnu" in tc or libc == "glibc"
