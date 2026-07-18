from maix import image
from base_page import BasePage
import json

class ScanPage(BasePage):
    detector = None
    _scan_image = None
    _scan_image_postion = [0,0,1,1]
    

    def __init__(self, name, disp, cam, cam_min):
        super().__init__(name, disp, cam, cam_min)
        self.detector = image.QRCodeDetector()
        self._scan_image = image.load("./images/Scan.png", format=image.Format.FMT_BGRA8888)
        self._scan_image_postion = [
            self.disp.width() // 2  - self._scan_image.width() // 2,
            self.disp.height() // 2 - self._scan_image.height() // 2,
            self._back_image.width(),
            self._back_image.height()
        ]

    def show(self):
        print("SHOW")
        self.active = True

    def hide(self):
        print("HIDE")
        self.active = False

    def hanlder_touch_pressed(self, x, y):
        print(f"x={x}, y={y}")

    def loop(self, img, ts_res):

        cam_img = self.cam.read()
        cam_min_img = self.cam_min.read()
        qrcodes = self.detector.detect(cam_min_img)
        for q in qrcodes:
            res_str = q.payload()
            try:
                res = json.loads(res_str)
                print("SCAN_RES", res)
                if all(val != '' for val in (res.get(k) for k in ["u", "t", "s", "d"]) if val is not None):
                    s_token = res.get("u")
                    s_type = res.get("t")
                    s_server = res.get("s")
                    s_data_id = res.get("d")
                    print(f"Token={s_token}, Type={s_type}, Server={s_server}, DataId={s_data_id}")
                    self.hide_toast()
                    self.emit_event('on_scan_result', s_token, s_type, s_server, s_data_id)
                    break
                else:
                    self.show_toast(3)

            except Exception as e:
                print('error', e)
                self.show_toast(3)

        img.draw_image(0,0,cam_img)
        
        self._scan_image_postion[1] += 10
        if self._scan_image_postion[1] >= self.disp.height() // 2 + 120 - 80:
            self._scan_image_postion[1] = self.disp.height() // 2 - 120 - 40

        img.draw_image(self._scan_image_postion[0], self._scan_image_postion[1] - self._scan_image_postion[3], self._scan_image)

        return img

    def init(self):
        print("INIT")