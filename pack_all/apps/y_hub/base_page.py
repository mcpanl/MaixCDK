from maix import image, time

class BasePage:
    active = False
    name = None
    disp = None
    cam = None
    cam_min = None

    _back_image = None
    _back_image_position = [0,0,1,1]

    _my_models_image = None
    _my_models_image_position = [0,0,1,1]

    _last_pressed = False

    _event_handlers = {}

    show_my_model_list = ["home", "scan"]

    _toast_message = None
    _toast_start_time = None
    _toast_duration = 3  # 显示 3 秒

    def __init__(self, name, disp, cam, cam_min):
        print("Init Page", name)
        self._last_pressed = False
        self._deferred_touch = None  # 用于存储延迟触摸信息
        self.continue_base_hanlder_touch_pressed = False
        self.name = name
        self.disp = disp
        self.cam = cam
        self.cam_min = cam_min
        self._back_image = image.load("./images/Back.png", format=image.Format.FMT_BGRA8888)
        self._back_image_position = [
            12,
            12,
            self._back_image.width(),
            self._back_image.height()
        ]

        self._my_models_image = image.load("./images/MyModels.png", format=image.Format.FMT_BGRA8888)
        self._my_models_image_position = [
            self.disp.width() - self._my_models_image.width() - 12,
            12,
            self._my_models_image.width(),
            self._my_models_image.height()
        ]

        self.internet_connection_required_img = image.load("./assets/images/internet_connection_required.png", format=image.Format.FMT_BGRA8888)
        self.internet_connection_required_img_xywh = [
            (self.disp.width() - self.internet_connection_required_img.width()) // 2,
            96,
            self.internet_connection_required_img.width(),
            self.internet_connection_required_img.height()
        ]

        self.token_expire_please_scan_again_img = image.load("./assets/images/token_expire_please_scan_again.png", format=image.Format.FMT_BGRA8888)
        self.token_expire_please_scan_again_img_xywh = [
            (self.disp.width() - self.token_expire_please_scan_again_img.width()) // 2,
            96,
            self.token_expire_please_scan_again_img.width(),
            self.token_expire_please_scan_again_img.height()
        ]

        self.no_response_please_try_again_img = image.load("./assets/images/no_response_please_try_again.png", format=image.Format.FMT_BGRA8888)
        self.no_response_please_try_again_img_xywh = [
            (self.disp.width() - self.no_response_please_try_again_img.width()) // 2,
            96,
            self.no_response_please_try_again_img.width(),
            self.no_response_please_try_again_img.height()
        ]

        self.wrong_qr_code_please_check_img = image.load("./assets/images/wrong_qr_code_please_check.png", format=image.Format.FMT_BGRA8888)
        self.wrong_qr_code_please_check_img_xywh = [
            (self.disp.width() - self.wrong_qr_code_please_check_img.width()) // 2,
            96,
            self.wrong_qr_code_please_check_img.width(),
            self.wrong_qr_code_please_check_img.height()
        ]

        self.pressed_already = False
        self.last_x = 0
        self.last_y = 0
        self.last_pressed = False

        self.toast_type = 0
    
    def hide_toast(self):
        self.toast_type = 0
    
    def show_toast(self, toast_type):
        self.toast_type = toast_type

    def _draw_toast(self, img):
        if self.toast_type == 0:
            return

        # 1: 网络错误，请检查网络连接
        if self.toast_type == 1:
            img.draw_image(self.internet_connection_required_img_xywh[0],
                        self.internet_connection_required_img_xywh[1],
                        self.internet_connection_required_img)
        
        # 2: 二维码已失效，请刷新后重试
        elif self.toast_type == 2:
            img.draw_image(self.token_expire_please_scan_again_img_xywh[0],
                        self.token_expire_please_scan_again_img_xywh[1],
                        self.token_expire_please_scan_again_img)
        
        # 3: 请检查二维码是否正确
        elif self.toast_type == 3:
            img.draw_image(self.wrong_qr_code_please_check_img_xywh[0],
                        self.wrong_qr_code_please_check_img_xywh[1],
                        self.wrong_qr_code_please_check_img)
        
        # 4: 请稍后再试
        else:
            img.draw_image(self.no_response_please_try_again_img_xywh[0],
                        self.no_response_please_try_again_img_xywh[1],
                        self.no_response_please_try_again_img)

    def _base_hanlder_touch_pressed(self, x, y):
        if self.is_in_button(x, y, self._back_image_position):
            self.hide_toast()
            self.emit_event('on_back_button_click')
        elif self.is_in_button(x, y, self._my_models_image_position):
            self.emit_event('on_my_model_button_click')
        
    def _base_loop(self, img, ts_res):
        touch_x, touch_y, pressed = ts_res

        # 防误触设计，模拟用户按压屏幕松开后才触发
        if touch_x != self.last_x or touch_y != self.last_y or pressed != self.last_pressed:
            self.last_x = touch_x
            self.last_y = touch_y
            self.last_pressed = pressed
        if pressed:
            self.pressed_already = True
        elif self.pressed_already:
            self.pressed_already = False

            self.handler_touch(ts_res)
            
            if self.continue_base_hanlder_touch_pressed == False:
                # 有些情况需要不执行 _base_hanlder_touch_pressed
                self._base_hanlder_touch_pressed(touch_x, touch_y)
            self.hanlder_touch_pressed(touch_x, touch_y)

        try:
            img = self.loop(img, ts_res)
        except Exception as e:
            print(e)
            pass

        img.draw_image(self._back_image_position[0], self._back_image_position[1], self._back_image)

        if self.name in self.show_my_model_list:
            img.draw_image(self._my_models_image_position[0],
                        self._my_models_image_position[1],
                        self._my_models_image)
        
        self._draw_toast(img)
        
        return img
    
    def handler_touch(self, ts_res):
        pass

    def hanlder_touch_pressed(self, x, y):
        pass

    def is_in_button(self, x, y, btn_pos):
        return x > btn_pos[0] and x < btn_pos[0] + btn_pos[2] and y > btn_pos[1] and y < btn_pos[1] + btn_pos[3]

    def add_event(self, event_name):
        """添加事件监听"""
        def decorator(handler):
            if event_name not in self._event_handlers:
                self._event_handlers[event_name] = []
            self._event_handlers[event_name].append(handler)
            return handler
        return decorator
    
    def emit_event(self, event_name, *args, **kwargs):
        """触发事件"""
        if event_name in self._event_handlers:
            for handler in self._event_handlers[event_name]:
                handler(self, *args, **kwargs)