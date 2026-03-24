#!/usr/bin/env python3
import os
import time
import signal
import subprocess

RUN_APP_FILE = "/tmp/run_app.txt"
LAUNCHER_PATH = "/maixapp/apps/launcher/launcher"

SLEEP_INTERVAL = 1


def log(msg):
    print(f"[daemon] {msg}", flush=True)


# -------------------------
# 初始化 run_app.txt
# -------------------------
def clear_run_file():
    try:
        with open(RUN_APP_FILE, "w") as f:
            f.write("")
    except:
        pass


# -------------------------
# 杀掉旧 launcher
# -------------------------
def kill_old_launcher():
    try:
        out = subprocess.check_output(["ps", "-ef"]).decode()
        for line in out.splitlines():
            if LAUNCHER_PATH in line and "grep" not in line:
                pid = int(line.split()[1])
                log(f"kill old launcher {pid}")
                os.kill(pid, signal.SIGTERM)
    except:
        pass


# -------------------------
# 启动 launcher（关键修复点）
# -------------------------
def start_launcher():
    try:
        dirname = os.path.dirname(LAUNCHER_PATH)
        filename = os.path.basename(LAUNCHER_PATH)

        log("start launcher")

        return subprocess.Popen(
            ["./" + filename],
            cwd=dirname,
            env=os.environ.copy()
        )
    except Exception as e:
        log(f"launcher start failed: {e}")
        return None


# -------------------------
# 读取 app
# -------------------------
def read_app():
    try:
        with open(RUN_APP_FILE, "r") as f:
            line = f.readline().strip()
            return line if line else None
    except:
        return None


# -------------------------
# 启动 app
# -------------------------
def start_app(path):
    try:
        if not os.path.exists(path):
            log(f"app not exist: {path}")
            return None

        dirname = os.path.dirname(path)
        filename = os.path.basename(path)

        log(f"start app: {path}")

        return subprocess.Popen(
            ["./" + filename],
            cwd=dirname
        )
    except Exception as e:
        log(f"start app failed: {e}")
        return None


# -------------------------
# 主循环
# -------------------------
def main():
    log("daemon start")

    clear_run_file()

    while True:
        try:
            # 1. 清理旧 launcher
            kill_old_launcher()

            # 2. 启动 launcher
            launcher = start_launcher()
            if not launcher:
                time.sleep(2)
                continue

            # 3. 等待 launcher 退出
            ret = launcher.wait()
            log(f"launcher exit code={ret}")

            # 🚨 crash 防抖
            if ret == -11:
                log("launcher crash, sleep...")
                time.sleep(2)

            # 4. 读取 app
            app = read_app()
            if not app:
                continue

            # 5. 启动 app
            proc = start_app(app)

            # 清空（防重复执行）
            clear_run_file()

            if not proc:
                continue

            # 6. 等待 app 结束
            proc.wait()
            log("app exit, back to launcher")

        except Exception as e:
            log(f"error: {e}")
            time.sleep(2)


if __name__ == "__main__":
    main()