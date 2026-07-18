import re
from maix import display, image
from loading import Loading
import threading
from multiprocessing import Process, Event as ProcessEvent, Lock as ProcessLock, Value as ProcessValue, Manager

disp = display.Display()
display_show_lock = ProcessLock()

font_size = 20
image.load_font("sourcehansans", "/maixapp/share/font/SourceHanSansCN-Regular.otf", size = font_size)  # 加载自定义字体
image.set_default_font("sourcehansans")  # 设置默认字体

loaindg = Loading(disp)
loading_thread = None

manager = Manager()
shared_dict = manager.dict()
shared_dict['show_loading'] = True

# 加载动画，import 逻辑放到加载动画后面用来以防黑屏等待提升用户体验
def loading_thread_main():
    global show_loading, loading_thread

    should_break = False

    while shared_dict['show_loading']:
        with display_show_lock:
            if shared_dict['show_loading']:
                img = loaindg.draw()
                disp.show(img)
            else:
                should_break = True
        
        if should_break:
            break

        time.sleep_ms(25)

loading_thread = threading.Thread(target=loading_thread_main, daemon=True)
loading_thread.start()

# init_img = image.Image(disp.width(), disp.height())
# logo_img = image.load("./images/logo.png")
# init_img.draw_image(disp.width() // 2 - logo_img.width() // 2, disp.height() // 2 - logo_img.height() // 2, logo_img)
# disp.show(init_img)

# str_size = image.string_size("Loading", scale=1.8, thickness=-1)
# init_img.draw_string(disp.width() // 2 - str_size.width() // 2, disp.height() // 2 - str_size.height() // 2 + logo_img.height() // 2 + 48, "Loading", scale=1.8, thickness=-1)
# disp.show(init_img)

active_page = "home"

from maix import key

longpress_lock = False

def on_user_key(key_id, state):
    '''
        this func called in a single thread
    '''
    
    global longpress_lock

    try:
        if key_id == 352:
            if state == 0 and longpress_lock == False:
                if active_page == "collect":
                    collect_page.handler_user_key()
            elif state == 1:
                longpress_lock = False
            elif state == 2:
                longpress_lock = True
    except Exception as e:
        pass

key_obj = key.Key(on_user_key)

from maix import camera, app, time, touchscreen, pinmap, gpio
from home import HomePage
from collect import CollectPage
from deploy import DeployPage
from scan import ScanPage
from demo import DemoPage
from model_list import ModelListPage
from model_item import ModelItemPage

import requests
import os
import zipfile
import random
import datetime
import hashlib
import hmac
import base64
import os
import shutil

import select
import socket
from utils import is_network_available


# Unix 域套接字路径
socket_path = "/tmp/my_socket"

client_socket = None
client_socket_status = False

socket_rx_string_buffer = ''


def send_socket_message(message):
    try:
        message_packet = f"@#{message}#@"
        print('[发送成功]\n', message_packet)
        if client_socket:
            client_socket.sendall(message_packet.encode())
    except Exception as e:
        print(e)


cam = camera.Camera(disp.width(), disp.height())
cam_min = cam.add_channel(disp.width() // 2, disp.height() // 2)
ts = touchscreen.TouchScreen()

model_dir = '/root/y_hub/models'


home_page = HomePage("home", disp, cam, cam_min)
scan_page = ScanPage("scan", disp, cam, cam_min)
collect_page = CollectPage("collect", disp, cam, cam_min)
deploy_page = DeployPage("deploy", disp, cam, cam_min)
demo_page = DemoPage("demo", disp, cam, cam_min)
model_list_page = ModelListPage("modelList", disp, cam, cam_min)
model_item_page = ModelItemPage("modelItem", disp, cam, cam_min)
# model_list_page.show()

pages = {
    "home": home_page,
    "scan": scan_page,
    "collect": collect_page,
    "deploy": deploy_page,
    "demo": demo_page,
    "modelList": model_list_page,
    "modelItem": model_item_page
}

# img = image.Image(disp.width(), disp.height(), format=image.Format.FMT_BGRA8888)
# img = home_page._base_loop(img, [0, 0, 0])
# disp.show(img)


def handler_socket_message(message: str):
    print("[收到消息]", message)
    cmds = message.split("\n")

    if cmds[0] == 'y_hub_0x5B':
        print("根据空间特征，返回图像检测对象特征值", cmds[1], cmds[2])
        # 1~5代表最中间、最靠左、最靠右、最靠上、最靠下
        type1 = cmds[1]

        # 1~5代表X坐标、Y坐标、宽度、高度、置信度。
        type2 = cmds[2]

        res = demo_page.get_target_by_condition(type1, type2)

        if res is not None:
            send_socket_message(f'y_hub_0x5B\nfull\n{res}')
        else:
            send_socket_message('y_hub_0x5B\nempty')
    elif cmds[0] == 'y_hub_0x5C':
        print("根据空间特征，返回图像检测对象物体类型", cmds[1])
        # 1~5代表最中间、最靠左、最靠右、最靠上、最靠下
        type1 = cmds[1]
        res = demo_page.get_target_by_condition(type1, None)

        if res is not None:
            send_socket_message(f'y_hub_0x5C\nfull\n{res["label"]}')
        else:
            send_socket_message('y_hub_0x5C\nempty')

    elif cmds[0] == 'y_hub_0x5D':
        print("根据物体类型，返回图像检测对象特征值", cmds[1], cmds[2])
        str1 = cmds[1]
        type1 = cmds[2]

        res = demo_page.get_label_value_nearest_center(str1, type1)
        if res is not None:
            send_socket_message(f'y_hub_0x5D\nfull\n{res}')
        else:
            send_socket_message('y_hub_0x5D\nempty')
    elif cmds[0] == 'y_hub_0x5E':
        print("返回图像分类的结果")
        res = demo_page.classifier_result

        if res is not None:
            send_socket_message(f'y_hub_0x5E\nfull\n{res["label"]}')
        else:
            send_socket_message('y_hub_0x5E\nempty')

    elif cmds[0] == 'y_hub_0x5F':
        print("返回图像分类结果的置信度")
        res = demo_page.classifier_result

        if res is not None:
            send_socket_message(f'y_hub_0x5F\nfull\n{int(res["score"] * 100)}')
        else:
            send_socket_message('y_hub_0x5F\nempty')

    elif cmds[0] == 'y_hub_0x70':
        print("加载特定ID的模型", cmds[1])

        try:
            model_index = cmds[1]
 
            # 重新扫描模型文件夹并更新数据
            model_list_page.data_initialized = False
            model_list_page.scan_model_folders()

            if int(model_index) - 1 >= len(model_list_page.model_list) or int(model_index) - 1 <= -1:
                print('模型索引不存在，无法加载')
                return

            model_item_page.model = model_list_page.model_list[int(model_index) - 1]
            model_item_page.emit_event('on_model_item_button_click', "deploy")
        except Exception:
            pass

        # if res is not None:
        #     send_socket_message(f'y_hub_0x5F\nfull\n{int(res["score"] * 100)}')
        # else:
        #     send_socket_message('y_hub_0x5F\nempty')


def socket_worker():
    global client_socket_status, client_socket, socket_rx_string_buffer
    while not app.need_exit():
        if not client_socket_status:
            if os.path.exists(socket_path):
                client_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                client_socket.connect(socket_path)
                client_socket_status = True
                print("Socket连接成功")

                send_socket_message('global_get_camera_flip_status')
            time.sleep_ms(500)
            continue
        
        if not client_socket:
            time.sleep_ms(500)
            continue

        ready_to_read, _, _ = select.select([client_socket], [], [], 0.25)  # 0.25秒超时

        if ready_to_read:
            # 接收服务端的消息
            data = client_socket.recv(1024)
            if data:
                print(f"Received from server: {data.decode()}")
                tmp_str = data.decode()
                socket_rx_string_buffer += tmp_str
                while True:
                    start_idx = socket_rx_string_buffer.find('@#')
                    end_idx = socket_rx_string_buffer.find('#@')
                    if start_idx != -1 and end_idx != -1 and start_idx < end_idx:
                        packet = socket_rx_string_buffer[start_idx+2 : end_idx]
                        
                        # print('[收到完整数据包]\n', packet)
                        cmds = packet.split("\n")
                        if cmds[0] == 'global_debug':
                            pass
                        else:
                            handler_socket_message(packet)
                        
                        socket_rx_string_buffer = socket_rx_string_buffer[end_idx+2:]
                    else:
                        # 没有完整数据包时退出循环
                        break
            else:
                # 如果接收到空数据，说明连接已关闭
                print("Connection closed by server")
                time.sleep_ms(500)

        time.sleep_ms(25)

accept_thread = threading.Thread(target=socket_worker)
accept_thread.daemon = True
accept_thread.start()

@collect_page.add_event("on_back_button_click")
def on_back_button_click(page):
    print('on_back_button_click', page)

    if page == home_page:
        app.set_exit_flag(True)
    elif page == scan_page:
        change_page("home")
    elif page == collect_page:
        change_page("home")
    elif page == deploy_page:
        change_page("home")
    elif page == demo_page:
        change_page("modelList")
    elif page == model_list_page:
        change_page("home")
    elif page == model_item_page:
        change_page("modelList")

@collect_page.add_event("on_my_model_button_click")
def on_my_model_button_click(page):
    print('on_my_model_button_click', page)
    if active_page == 'home' or active_page == 'scan':
        change_page("modelList")

@collect_page.add_event("on_model_list_edit")
def on_model_list_edit(page, model):
    model_item_page.model = model
    change_page("modelItem")

@collect_page.add_event("on_model_item_button_click")
def on_model_item_button_click(page, name):
    if name == "delete":
        # 如果删除的这条是最后一条数据则自动翻到上一页
        if model_list_page.pagination_list and len(model_list_page.pagination_list[model_list_page.current_page - 1]) == 1 and model_list_page.current_page > 1:
            model_list_page.current_page -= 1

        print("删除", model_item_page.model)
        shutil.rmtree(model_item_page.model["dir_path"])
        
        # 删除后重新扫描模型文件夹并重置数据初始化状态
        model_list_page.data_initialized = False
        model_list_page.scan_model_folders()
        
        time.sleep(0.5)
        change_page("modelList")
        
    elif name == "deploy":
        print("部署", model_item_page.model)
        demo_page.model_path = model_item_page.model["model_path"]
        demo_page.reset()
        time.sleep(0.5)
        change_page("demo")

@scan_page.add_event("on_scan_result")
def on_scan_result(page, s_token, s_type, s_server, s_data_id):
    # 扫描训练或者部署二维码
    if s_type == 0:
        print("前往数据集页面")
        collect_page.data_id = s_data_id
        collect_page.server_type = s_server
        collect_page.token = s_token
        change_page("collect")
    elif s_type == 1:
        print("前往部署页面")
        deploy_page.download_status = "WaitMeatInfo"
        deploy_page.token = s_token
        if s_server:
            deploy_page.server_type = s_server
        change_page("deploy")
    else:
        print("未知Type暂不处理", s_type)

@home_page.add_event("on_home_button_click")
def on_home_button_click(page, name):
    if name == "collect":
        change_page("scan")
    elif name == "deploy":
        change_page("scan")

# 帧率计算变量
frame_count = 0
last_time = time.ticks_ms()
fps = 0

last_tick = time.time()

def loop_active_page():
    global cam, cam_min, last_time, frame_count, last_tick

    if int(time.time()) - last_tick > 5:
        send_socket_message('PASS')
        last_tick = time.time()
            

    t0 = time.ticks_ms()
    ts_res = ts.read()
    t1 = time.ticks_ms()

    current_page = pages.get(active_page)
    if current_page is None:
        return

    t2 = time.ticks_ms()
    img = image.Image(disp.width(), disp.height(), format=image.Format.FMT_BGRA8888)
    t3 = time.ticks_ms()

    img = current_page._base_loop(img, ts_res)
    t4 = time.ticks_ms()

    with display_show_lock:
        if not shared_dict['show_loading']:
            disp.show(img) # 显示到屏幕

    t5 = time.ticks_ms()

    # print("触摸读取耗时:", time.ticks_diff(t1, t0), "ms")
    # print("图像创建耗时:", time.ticks_diff(t3, t2), "ms")
    # print("base_loop 调用耗时:", time.ticks_diff(t4, t3), "ms")
    # print("显示图像耗时:", time.ticks_diff(t5, t4), "ms")
    # print("==============")
    frame_count += 1
    current_time = time.ticks_ms()
    if current_time - last_time >= 1000:  # 每1秒计算一次
        fps = frame_count
        frame_count = 0
        last_time = current_time
        # print(fps)
    time.sleep_ms(1)

def change_page(name):
    global active_page

    print('change_page', name)

    active_page = name

    if active_page == "home":
        home_page.show()
    elif active_page == "collect":
        collect_page.reset()
        collect_page.show()
    elif active_page == "deploy":
        deploy_page.show()
    elif active_page == "modelList":
        model_list_page.show()
    elif active_page == "modelItem":
        model_item_page.show()

def __upload_azure_blob(auth_data, img_bytes, folder_id, filename) -> tuple:

    try:

        # 从返回的 JSON 中获取必要字段
        oss_host = auth_data['host']  # 或 data['ossConfig']['ossHost']
        prefix_dir = auth_data['ossConfig']['prefixDir']  # 'test/mtraining/'
        bucket = auth_data['ossConfig']['bucket']  # 'private'
        sts_token = auth_data['stsToken']  # URL query 参数

        # 拼接文件路径
        file_path = f"{bucket}/{prefix_dir}{folder_id}/{filename}"

        # 拼接完整 URL（确保 query 部分不被双重编码）
        base_url = f"https://{oss_host}/{file_path}"
        final_url = f"{base_url}?{sts_token}"

        print("拼接后的 URL：")
        print(final_url)
    except Exception as e:
        print(e)
        return False, "获取OSS信息异常"

    upload_success = False

    if final_url:
        headers = {
            "x-ms-blob-type": "BlockBlob"
        }

        try:
            print("= 开始PUT上传")
            res = requests.put(final_url, data = img_bytes, headers=headers)

            print("上传结果", res.status_code)

            if res.status_code == 201:
                upload_success = True
            else:
                return False, "PUT上传失败"
        except Exception as e:
            print(e)
            return False, "PUT上传异常"

    return upload_success, ""


def __upload_aliyun_oss(auth_data, img_bytes, folder_id, filename) -> tuple:

    try:
        print("= 开始阿里云OSS上传")
        
        try:
            # 从auth_data中提取必要信息
            access_key_id = auth_data['accessKeyId']
            access_key_secret = auth_data['accessKeySecret']
            sts_token = auth_data['stsToken']
            oss_config = auth_data['ossConfig']
            bucket = oss_config['bucket']
            endpoint = oss_config.get('ossHost', '').replace('private-res-cn.private-res-cn.', 'private-res-cn.')
            prefix_dir = oss_config.get('prefixDir', '').rstrip('/')
            
            # 构建对象名和资源路径
            key = f"{prefix_dir}/{folder_id}/{filename}".lstrip('/')
            resource = f"/{bucket}/{key}"
            
            # 构建URL
            endpoint = endpoint.replace(f"{bucket}.", "")
            url = f"https://{bucket}.{endpoint}/{key}"
            print(f"= 阿里云OSS上传URL: {url}")
            
            # 生成RFC 1123格式的日期
            date = datetime.datetime.utcnow().strftime('%a, %d %b %Y %H:%M:%S GMT')
            
            # 计算文件MD5
            content_md5 = base64.b64encode(hashlib.md5(img_bytes).digest()).decode()
            
            # 构建规范化的OSS头
            oss_headers = f"x-oss-security-token:{sts_token}\n"
            
            # 构建待签名字符串
            string_to_sign = "\n".join([
                "PUT",
                content_md5,
                "application/octet-stream",
                date,
                oss_headers + resource
            ])
            
            # 计算签名
            h = hmac.new(
                access_key_secret.encode('utf-8'),
                string_to_sign.encode('utf-8'),
                hashlib.sha1
            )
            signature = base64.b64encode(h.digest()).decode()
            
            # 构建请求头
            headers = {
                'Date': date,
                'Content-Type': 'application/octet-stream',
                'Content-MD5': content_md5,
                'Authorization': f"OSS {access_key_id}:{signature}",
                'x-oss-security-token': sts_token
            }
            
            # 上传文件到OSS
            res = requests.put(url, headers=headers, data=img_bytes)
                
            if res.status_code != 200:
                print(f"阿里云OSS上传响应: {res.status_code} {res.text}")
                return False, "阿里云OSS上传失败"
                
            print("= 阿里云OSS上传成功")
            return True, ""
        except Exception as e:
            print(f"阿里云OSS上传失败: {e}")
            return False, f"阿里云OSS上传失败: {str(e)}"

    except Exception as e:
        print(f"阿里云OSS处理异常: {e}")
        return False, f"阿里云OSS处理异常: {str(e)}"

def upload_dataset(img, server_type, token, data_id, format = "jpg"):
    if type(img) == str:
        from PIL import Image
        from io import BytesIO

        jpeg = BytesIO()
        img = Image.open(img)
        img.save(jpeg, format="JPEG")
        img_bytes = jpeg.getvalue()
    elif type(img) == bytes:
        img_bytes = img
    else:
        raise Exception("img must be a path or bytes")

    # 获取当前时间戳（毫秒级）
    timestamp = int(time.time() * 1000)

    # 生成4位随机数
    random_num = random.randint(1000, 9999)  # 1000-9999之间的随机数
    folder_id = data_id
    filename = f"{timestamp}{random_num}.{format}"

    # 0 为国服，1 为欧服
    if server_type == 1:
        upload_method = __upload_azure_blob
        api_prefix = 'https://dtc-api.mblock.cc/mtraining'
    else:
        upload_method = __upload_aliyun_oss
        api_prefix = 'https://d2vapi.makeblock.com/mtraining'

    print(f"= 开始获取OSS验证信息: {api_prefix}")
    headers = {"ctoken": token}

    try:
        res = requests.get(api_prefix + '/v1/device/upload/token', headers=headers)

        print('res', res)
        
        if res.status_code != 200:
            collect_page.show_toast(1)
            return False, "Server refused"

        response = res.json()

        print('response', response)

        if response['code'] != 0:
            # {'code': 200007, 'msg': '二维码失效，请刷新后重试', 'data': {}}
            if response['code'] == 200007:
                collect_page.show_toast(2)
                return False, "Token expire"
            
            collect_page.show_toast(4)
            return False, "Unknown Error"

        data = response['data']

        retries = 0
        while retries < 3:

            # 调用上传函数
            upload_success, error_msg = upload_method(
                auth_data=data,
                img_bytes=img_bytes, 
                folder_id=folder_id, 
                filename=filename, 
            )

            if upload_success:
                break

            retries += 1
            print(f"= 上传失败，重试第{retries}次")

        if upload_success:
            headers = {
                "ctoken": token
            }

            try:
                print("= 开始通知上传结果")
                res = requests.post(api_prefix + '/v1/device/img/upload', json={
                    "url": f"{folder_id}/{filename}"
                }, headers=headers)

                res_data = res.json()

                print(res_data)

                if res.status_code == 200 and res_data.get("code") == 0:
                    return True, ""
                else:
                    return False, "通知上传结果失败"
            except Exception as e:
                print(e)
                return False, "通知上传结果异常"
        else:
            collect_page.show_toast(4)
            return False, error_msg
    except Exception as e:
        collect_page.show_toast(4)
        return False, "Network error"

def download_task(url, filename, file_full_path):
    """下载任务线程函数"""
    
    try:
        with requests.get(url, stream=True) as response:
            response.raise_for_status()
            
            # 获取文件总大小（可能不存在）
            total_size = int(response.headers.get('Content-Length', 0))


            # 创建文件保存目录
            os.makedirs(os.path.dirname(file_full_path), exist_ok=True)

            # 开始下载
            downloaded = 0
            with open(file_full_path, 'wb') as file:
                for chunk in response.iter_content(chunk_size=4096):
                    if chunk:
                        file.write(chunk)
                        downloaded += len(chunk)
                        
                        # 计算下载进度
                        progress = (downloaded / total_size * 100) if total_size > 0 else -1
                        
                        # 更新进度信息
                        print(f"下载进度 {progress:.2f}%", end='\r', flush=True)
                        deploy_page.download_progress = int(progress)

            print("下载完成")
            deploy_page.download_status = "WaitZip"
                
    except Exception as e:
        print("下载失败", e)
        deploy_page.download_status = "Error"
        deploy_page.error_msg = 'Network error, please check and try again.'

def get_camera_flip_status():
    try:
        with open('/mk/camera_flip.txt', 'r', encoding='utf-8') as file:
            status = file.read().strip()
            return status == "1"
    except Exception as e:
        print(f"读取翻转状态失败: {e}")
        return False  # 默认不翻转

def network_worker():
    while not app.need_exit():
        is_flip = get_camera_flip_status()

        cam.vflip(is_flip)
        cam_min.vflip(is_flip)
        
        # 收集页面
        if active_page == "collect" and len(collect_page.take_photo_file_list) > 0:
            # 检查网络连接，如果网络连接失败，则显示网络错误
            if is_network_available() == False:
                collect_page.show_toast(1)
                continue
            else:
                collect_page.hide_toast()

            file_full_path = collect_page.take_photo_file_list[0]
            print('file_full_path', file_full_path)
            with open(file_full_path, 'rb') as file:
                content = file.read()  # 读取全部内容
                print(len(content))
                print("开始上传", file_full_path)
                
                upload_res, msg = upload_dataset(content, collect_page.server_type, collect_page.token, collect_page.data_id, "jpeg")

                if upload_res == True:
                    print("上传成功", file_full_path)
                    collect_page.take_photo_upload_list.append(file_full_path)

                collect_page.take_photo_file_list.pop(0)
        
        # 部署模型页面
        elif active_page == "deploy":
            print(222)

            # 检查网络连接，如果网络连接失败，则显示网络错误
            if is_network_available() == False:
                deploy_page.download_status = "Error"
                deploy_page.error_msg = 'Internet connection required.'
                continue

            if deploy_page.download_status == "WaitMeatInfo" and deploy_page.token is not None:
                print("开始获取模型基础信息")
                headers = {
                    "ctoken": deploy_page.token,
                }
                try:
                    server_type = deploy_page.server_type
                    # 0 为国服，1 为欧服
                    if server_type == 1:
                        api_prefix = 'https://dtc-api.mblock.cc/mtraining'
                    else:
                        api_prefix = 'https://d2vapi.makeblock.com/mtraining'

                    info_url = f'{api_prefix}/v1/device/task/result'
                    print("Get Url = ", info_url)

                    res = requests.get(info_url, headers=headers)
                    if res.status_code == 200:
                        _data = res.json()
                        data = _data.get("data")

                        download_url = data.get("url")
                        model_id = data.get("id")
                        model_name = data.get("name")
                        model_type = data.get("type")

                        print("获取基础信息成功", model_id, model_name, model_type)

                        if model_id == None:
                            deploy_page.download_status = "Error"
                            deploy_page.error_msg = 'Token expire. Please scan again.'
                            continue

                        deploy_page.download_url = download_url
                        deploy_page.model_id = model_id
                        deploy_page.model_name = model_name
                        deploy_page.model_type = model_type
                        deploy_page.model_full_name = f'{deploy_page.server_type}_{model_id}_{model_type}'

                        model_path = f"{model_dir}/{deploy_page.model_full_name}/"
                        # 判断本地是否已存在并包含mud文件
                        if os.path.exists(model_path):
                            mud_files = [f for f in os.listdir(model_path) if f.endswith('.mud')]
                            if mud_files:
                                mud_file_full_path = os.path.join(model_path, mud_files[0])
                                if os.path.getsize(mud_file_full_path) > 0:
                                    print("本地已有有效模型文件，跳过下载与解压")
                                    deploy_page.download_status = "Success"
                                    deploy_page.download_progress = 100
                                    demo_page.model_path = mud_file_full_path
                                    demo_page.reset()
                                    
                                    # 部署成功后重置模型列表的数据初始化状态，确保下次显示时能刷新模型列表
                                    model_list_page.data_initialized = False
                                    
                                    time.sleep(0.5)
                                    change_page("demo")
                                    continue

                        deploy_page.download_progress = 0
                        deploy_page.download_status = "Downloading"
                except Exception as e:
                    print("获取模型信息失败:", e)
                    deploy_page.download_status = "Error"
                    deploy_page.error_msg = 'Failed to obtain model information.'

            if deploy_page.download_status == "Downloading" and deploy_page.download_url != None:
                print("开始下载部署模型", deploy_page.download_url)
                download_task(deploy_page.download_url, f"{deploy_page.model_full_name}.zip", f"{model_dir}/{deploy_page.model_full_name}.zip")
                
            if deploy_page.download_status == "WaitZip":
                print("开始解压文件")
                extract_path = f"{model_dir}/{deploy_page.model_full_name}/"
                zip_path = f"{model_dir}/{deploy_page.model_full_name}.zip"

                with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                    zip_ref.extractall(extract_path)
                print("解压完毕")

                # 写入 model_name.txt
                model_name_txt_path = os.path.join(extract_path, "model_name.txt")
                with open(model_name_txt_path, "w", encoding="utf-8") as f:
                    f.write(deploy_page.model_name)

                mud_files = [f for f in os.listdir(extract_path) if f.endswith('.mud')]
                if len(mud_files) > 0:
                    print("mud", mud_files[0])
                    mud_file_full_path = os.path.join(extract_path, mud_files[0])
                    deploy_page.download_status = "Success"
                    demo_page.model_path = mud_file_full_path
                    demo_page.reset()
                    
                    # 部署成功后重置模型列表的数据初始化状态，确保下次显示时能刷新模型列表
                    model_list_page.data_initialized = False
                    
                    time.sleep(0.5)
                    change_page("demo")
                else:
                    deploy_page.download_status = "Error"

        time.sleep(0.2)

_thread = threading.Thread(target=network_worker)
_thread.daemon = True
_thread.start()


def key_init():
    global key_swith_mid, key_swith_right, key_swith_left
    pinmap.set_pin_function("A22", "GPIOA22")
    pinmap.set_pin_function("A23", "GPIOA23")
    pinmap.set_pin_function("A25", "GPIOA25")

    try:
        key_swith_mid = gpio.GPIO("GPIOA22", gpio.Mode.IN)
    except Exception as e:
        print(e)

    try:
        key_swith_right = gpio.GPIO("GPIOA23", gpio.Mode.IN)
    except Exception as e:
        print(e)

    try:
        key_swith_left = gpio.GPIO("GPIOA25", gpio.Mode.IN)
    except Exception as e:
        print(e)
    
    print("key_init done")

def key_scan():
    global rec_display, back
    
    last_key_time = time.ticks_ms()
    last_key_state = 0
    key_down_count = 0
    key_init()
    while not app.need_exit():
        # 实现10ms扫描一次按键
        if time.ticks_ms() - last_key_time > 60:
            last_key_time = time.ticks_ms()
            if key_swith_mid.value() == 0:
                if key_down_count == 2:
                    # key_down_count = 0
                    back = True

                    if active_page == 'modelItem':
                        model_item_page.show_delete_popup = False # 后退页面之前先隐藏弹窗
                        model_item_page.continue_base_hanlder_touch_pressed = False

                    on_back_button_click(pages[active_page])
                    # app.set_exit_flag(True)
                    # break
                key_down_count += 1
            elif key_swith_right.value() == 0:
                # print(f'key_swith_right:{key_down_count}')
                if key_down_count == 2:
                    print('d+1')
                key_down_count += 1
            elif key_swith_left.value() == 0:
                # print(f'key_swith_left:{key_down_count}')
                if key_down_count == 2:
                    print('d-1')
                key_down_count += 1
            else:
                key_down_count = 0
        time.sleep_ms(10)

key_thread = threading.Thread(target=key_scan)
key_thread.daemon = True
key_thread.start()

def main():
    shared_dict['show_loading'] = False
    while not app.need_exit():
        loop_active_page()

main()

# _thread2 = threading.Thread(target=main)
# _thread2.daemon = True
# _thread2.start()

# while not app.need_exit():
    # time.sleep(1)