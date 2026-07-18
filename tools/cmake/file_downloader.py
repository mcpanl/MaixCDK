'''
    @author neucrack
    @date 2020.10.10
    @update 2022.3.12 fix download range and optimize for short file
    @license MIT
'''

import threading
import requests
import os
import sys
import time
# from progress.bar import Bar

from threading import Lock
import hashlib
import tarfile
import zipfile
import lzma
import json

sdk_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

lock = Lock()

def bytes2human(n_bytes):
    #return B if n_bytes < 1024, KiB if n_bytes < 1024*1024, MiB if < 1024*1024*1024
    if n_bytes < 1024:
        return "{} B".format(n_bytes)
    elif n_bytes < 1024*1024:
        return "{:.2f} KiB".format(n_bytes/1024)
    elif n_bytes < 1024*1024*1024:
        return "{:.2f} MiB".format(n_bytes/1024/1024)
    else:
        return "{:.2f} GiB".format(n_bytes/1024/1024/1024)

def unzip_file(zip_file, dst_dir):
    ext = os.path.splitext(zip_file)[1]
    if ext == ".zip":
        with zipfile.ZipFile(zip_file, 'r') as z:
            z.extractall(dst_dir)
    elif zip_file.endswith(".tar.gz") or zip_file.endswith(".tgz"):
        with tarfile.open(zip_file, 'r:gz') as t:
            t.extractall(dst_dir)
    elif ext == ".xz" and zip_file.endswith(".tar.xz"):
        with tarfile.open(zip_file, 'r:xz') as t:
            t.extractall(dst_dir)
    elif ext == ".xz":
        with lzma.open(zip_file, 'r') as f:
            with open(os.path.join(dst_dir, os.path.basename(zip_file)[:-3]), 'wb') as w:
                w.write(f.read())
    else:
        print("Error: file %s unsupported file type %s" % (zip_file, ext))
        sys.exit(1)

def check_sha256sum(file, sum):
    with open(file, "rb") as f:
        sha256sum=hashlib.sha256(f.read()).hexdigest()
    return sha256sum == sum, sha256sum

def _remove_pkg_and_temp(pkg_path):
    if os.path.exists(pkg_path):
        os.remove(pkg_path)
    name = os.path.basename(pkg_path)
    temp_path = os.path.join(os.path.dirname(pkg_path), ".{}.temp".format(name))
    if os.path.exists(temp_path):
        os.remove(temp_path)

def _download_item_try_urls(item, pkg_path, headers):
    '''
    Try item["url"] then each entry in item["urls"]. On network/sha256 failure remove pkg and try next.
    '''
    candidates = []
    if item.get("url"):
        candidates.append(item["url"])
    for u in item.get("urls") or []:
        if u and u not in candidates:
            candidates.append(u)
    if not candidates:
        print("-- Error: no download url in item", item.get("filename"))
        sys.exit(1)

    last_sha_msg = None
    for cand_idx, try_url in enumerate(candidates):
        print("-- try url [{}/{}]: {}".format(cand_idx + 1, len(candidates), try_url))
        err_times = 3
        delay_time = 0
        while err_times > 0:
            try:
                _remove_pkg_and_temp(pkg_path)
                d = Downloader(try_url, pkg_path, max_thead_num=8, force_write=True, headers=headers)
                d.start()
                break
            except Exception as e:
                print("-- download failed:", e)
                err_times -= 1
                if err_times > 0:
                    delay_time += 5
                    print(f"-- retry same url after {delay_time}s ({err_times} left)")
                    time.sleep(delay_time)
                else:
                    _remove_pkg_and_temp(pkg_path)
                    break
        else:
            # broke by success
            pass
        if not os.path.exists(pkg_path):
            continue
        if item.get("sha256sum"):
            ok, sha256sum = check_sha256sum(pkg_path, item["sha256sum"])
            if ok:
                return True
            last_sha_msg = (item["sha256sum"], sha256sum)
            print("-- sha256 mismatch after download, expected {}, got {} — try next url if any".format(
                item["sha256sum"], sha256sum))
            _remove_pkg_and_temp(pkg_path)
            continue
        return True
    if last_sha_msg:
        print("-- Error: sha256sum check failed after all urls, should be {}, but last file's sha256sum was {}.\n   Please download manually to {}".format(
            last_sha_msg[0], last_sha_msg[1], pkg_path))
    else:
        print("-- Error: download failed for {} from all urls".format(item.get("filename")))
    sys.exit(1)

class DownloaderThread(threading.Thread):
    def __init__(self, url, fd, range: tuple, callback, headers):
        threading.Thread.__init__(self)
        self.callback = callback
        self.url = url
        self.fd = fd
        self.range = range
        self.headers = headers

    def run(self):
        headers = {
            "Range":"bytes=%s-%s" %(self.range[0], self.range[1] - 1)
        }
        headers.update(self.headers)
        # print(f"-- download range [{self.range[0]} - {self.range[1]})")
        res = requests.get(self.url, headers=headers)
        if res.status_code != 206:
            raise Exception("download file error, download not support range download")
        lock.acquire()
        self.fd.seek(self.range[0])
        self.fd.write(res.content)
        self.fd.flush()
        lock.release()
        # print(f"-- download range [{self.range[0]} - {self.range[1]}) complete")
        self.callback(self.range)

class Downloader:
    def __init__(self, url, save_path, max_thead_num = 8, callback=None, force_write=False, progress_bar = True, headers = {}):
        self.progress_bar = progress_bar
        self.url = url
        self.max_thead_num = max_thead_num
        self.headers = headers
        self.save_path = os.path.abspath(os.path.expanduser(save_path))
        name = os.path.basename(self.save_path)
        temp_name = ".{}.temp".format(name)
        self.temp_path = os.path.join(os.path.dirname(self.save_path), temp_name)
        os.makedirs(os.path.dirname(self.save_path), exist_ok=True)
        self.callback = callback
        self.data_content = None
        headers = self.headers.copy()
        while 1:
            res = requests.head(self.url, headers=headers)
            # 302 redirect
            if res.status_code == 302 or res.status_code == 301:
                self.url = res.headers['Location']
                print(f"redirect to {self.url}")
                res = requests.head(self.url, headers=headers)
            else:
                break
        if res.status_code != 200:
            raise  Exception("download file error, code: {}, content: {}".format(res.status_code, res.content.decode()))
        try:
            self.total = int(res.headers['Content-Length'])
        except Exception:
            self.total = 0
        headers["Range"] = "bytes=0-1"
        res = requests.get(self.url, headers=headers)
        try:
            if not 'Content-Length' in res.headers:
                print("-- [WARNING] get content-length failed, will just download")
                if res.status_code == 200:
                    print("-- get content-length failed, try directly download, it may be slow")
                    res = requests.get(self.url, headers=self.headers)
                    if res.status_code == 200:
                        self.data_content = res.content
                        print("-- download file size: {}".format(bytes2human(len(self.data_content))))
                    else:
                        raise Exception("Download file error: " + res.status_code)
            else:
                if res.status_code != 206:
                    self.total = len(res.content)
                    print("-- get content-length failed, try directly download, it may be slow")
                    res = requests.get(self.url, headers=self.headers)
                    if res.status_code == 200:
                        self.data_content = res.content
                        print("-- download file size: {}".format(bytes2human(len(self.data_content))))
                    else:
                        raise Exception("Download file error: " + res.status_code)
                    print("-- download complete, file size: {}".format(bytes2human(self.total)))
        except Exception as e:
            print("-- download failed, error: {}, return headers: {}".format(e, res.headers))
            raise e
        self.downloaded = 0
        if not self.data_content:
            if self.total <= 0:
                raise  Exception("file size error")
            # update max_thread_num for short file
            if self.total < 1024 * self.max_thead_num:
                self.max_thead_num = self.total // 1024 + (1 if self.total % 1024 != 0 else 0)
            if self.progress_bar:
                print("-- Download file size: {}".format(bytes2human(self.total)))
        if os.path.exists(self.save_path) and not force_write:
            raise Exception("file already exists")
        dir_path = os.path.dirname(save_path)
        if not os.path.exists(dir_path):
            os.makedirs(dir_path)
            # self.bar = Bar("Downloading", max = self.total,  suffix='%(percent)d%% - %(index)d/%(max)d - %(eta)ds')
            # self.bar.next(0)

    def __chunks(self):
        start = 0
        self.chunk_size = self.total//self.max_thead_num
        for i in range(self.max_thead_num):
            if i == self.max_thead_num - 1: # last chunk
                last_chunk_size = self.total - self.chunk_size * i
                yield (start, start + last_chunk_size)
            else:
                yield (start, start + self.chunk_size)
            start += self.chunk_size

    def on_done(self, d_range):
        if self.progress_bar:
            self.downloaded += d_range[1] - d_range[0]
            print("-- Download {}% ({}/{})".format(self.downloaded * 100 // self.total, bytes2human(self.downloaded), bytes2human(self.total)))
            # self.bar.next(d_range[1] - d_range[0])

    def start(self):
        with open(self.temp_path, "wb"):
            pass
        if self.data_content:
            with open(self.temp_path, "ab") as f:
                f.write(self.data_content)
        else:
            fds = []
            ths = []
            with open(self.temp_path, "rb+") as fd_w:
                for d_range in self.__chunks():
                    # create new fd for multiple thread
                    dup_fno = os.dup(fd_w.fileno())
                    fd = os.fdopen(dup_fno,'rb+', -1)
                    fds.append(fd)
                    # create new thread
                    th = DownloaderThread(self.url, fd, d_range, self.on_done, self.headers)
                    th.daemon = True
                    th.start()
                    ths.append(th)
                # wait for all thread exit
                for th in ths:
                    th.join()
                for fd in fds:
                    fd.close()
        # delete old file
        if os.path.exists(self.save_path):
            os.remove(self.save_path)
        # rename
        os.rename(self.temp_path, self.save_path)
        if self.progress_bar:
            print("")

def check_download_items(items):
    keys = ["url", "sha256sum", "path"]
    for item in items:
        for key in keys:
            if key not in item:
                print("-- Error: {} not found in download item: {}".format(key, item))
                sys.exit(1)
        if not item["url"]:
            print("-- Error: url not found in download item: {}".format(item))
            sys.exit(1)
        if not item["filename"]:
            item["filename"] = os.path.basename(item["url"])
        if not item["path"]:
            print("-- Error: path not found in download item: {}, for example, toolchain can be 'toolchains/board_name'".format(item))
            sys.exit(1)
        if not "urls" in item:
            item["urls"] = []
        if not "sites" in item:
            item["sites"] = []
        if not "extract" in item:
            item["extract"] = True
        if not item["sha256sum"]:
            raise Exception("\n--!! [WARNING] sha256sum not found in download item: {}\n".format(item))

def download_extract_files(items):
    '''
        @items = [
            {
                "url": "http://****",
                "urls":[], # backup urls
                "sites":[], # backup sites, user can manually download
                "sha256sum": "****",
                "filename": "****",
                "path": "toolchains/m2dock", # will download to sdk_path/dl/pkgs/path/filename
                                             # and extract package to sdk_path/dl/extracted/path/
            }
    '''
    check_download_items(items)
    for i, item in enumerate(items):
        item["pkg_path"] = os.path.join(sdk_path, "dl", "pkgs", item["path"], item["filename"])

    for item in items:
        pkg_path = item["pkg_path"]
        extract_dir = os.path.join(sdk_path, "dl", "extracted", item["path"])
        need_download = True
        if os.path.exists(pkg_path):
            if item.get("sha256sum"):
                ok, sha256sum = check_sha256sum(pkg_path, item["sha256sum"])
                if ok:
                    need_download = False
                else:
                    print("-- Removing invalid existing file (wrong sha256 {}): {}".format(sha256sum, pkg_path))
                    _remove_pkg_and_temp(pkg_path)
            else:
                need_download = False

        if need_download:
            print("\n-------------------------------------------------------------------")
            print("-- Downloading {} save to: {}{}{}".format(
                item["filename"], pkg_path,
                "\n   primary url: {}".format(item["url"]) if item.get("url") else "",
                "\n   backup urls: {}".format(item.get("urls")) if item.get("urls") else "",
                "\n   sites: {}".format(item["sites"]) if item.get("sites") else ""))
            print("-------------------------------------------------------------------\n")
            headers = {}
            if "user-agent" in item:
                headers["User-Agent"] = item["user-agent"]
            _download_item_try_urls(item, pkg_path, headers)
        # extract_dir not empty means already extracted, continue
        need_extract = False
        if "extract" in item and not item["extract"]:
            continue
        renamed_files = []
        if "rename" in item:
            print(item)
            for _from, _to in item["rename"].items():
                from_path = os.path.join(extract_dir, _from)
                to_path = os.path.join(extract_dir, _to)
                renamed_files.append((from_path, to_path))
        if os.path.exists(extract_dir):
            files = os.listdir(extract_dir)
            files_final = []
            for f in files:
                if f.endswith(".extracting"):
                    need_extract = True
                    break
                files_final.append(f)
            for from_path, to_path in renamed_files:
                if not os.path.exists(to_path):
                    need_extract = True
            for file in item.get("check_files", []):
                path = os.path.join(extract_dir, file)
                if not os.path.exists(path):
                    need_extract = True
                    break
            if not need_extract:
                need_extract = len(files_final) == 0
            if not need_extract:
                print("-- {} already extracted, skip".format(os.path.join("dl", "pkgs", item["path"], item["filename"])))
                continue
        # extract
        print("-- Extracting {} to {}".format(pkg_path, extract_dir))
        # write unzip tmp file to extract_dir/filename.extracting
        flag_file = os.path.join(extract_dir, item["filename"] + ".extracting")
        os.makedirs(os.path.dirname(flag_file), exist_ok=True)
        with open(flag_file, "wb"):
            pass
        unzip_file(pkg_path, extract_dir)
        for from_path, to_path in renamed_files:
            if not os.path.exists(from_path):
                raise Exception("rename file failed, {} not found. (to {})".format(from_path, to_path))
            os.rename(from_path, to_path)
        os.remove(flag_file)

if __name__ == "__main__":
    items_str = sys.argv[1]
    if os.path.exists(items_str):
        with open(items_str, "r") as f:
            items_str = f.read()
    else:
        items_str = ";".join(sys.argv[1:])
    items_str = items_str.replace("'", '"')
    items = items_str.split(";")
    for i, item in enumerate(items):
        try:
            items[i] = json.loads(item)
        except Exception as e:
            print("-- Error: parse json failed, content: {}".format(item))
            raise e
    download_extract_files(items)
