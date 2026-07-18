from maix import image, time
from utils import is_in_button



# 删除确认弹窗
class DeletePopup:
    cancel_btn_x = 32
    cancel_btn_y = 358
    confirm_btn_x = 332
    confirm_btn_y = 358
    close_btn_x = 562
    close_btn_y = 8
    disp = None
    time_initial = time.ticks_ms()
    time_recorded = False

    def __init__(self, disp):

        self.disp = disp
        self.cancel_btn_img = image.load("./assets/images/cancel_btn.png", image.Format.FMT_RGBA8888)
        self.confirm_btn_img = image.load("./assets/images/confirm_btn.png", image.Format.FMT_RGBA8888)
        self.close_btn_img = image.load("./assets/images/close_btn.png", image.Format.FMT_RGBA8888)
    
    # 绘制弹窗界面
    def draw(self, img):
        if self.disp == None:
            print("dispay 不能为空")
            return
        
        popup_title = "Tips"
        popup_content = "Confirm to forget?"
        popup_text_scale = 1.6
        popup_title_w, popup_title_h = image.string_size(popup_title, popup_text_scale)
        popup_content_w, popup_content_h = image.string_size(popup_content, popup_text_scale)
        
        img.draw_rect(0, 0, self.disp.width(), self.disp.height(), image.Color(0,0,0,0.4, image.Format.FMT_RGBA8888), -1)
        img.draw_string(self.disp.width() // 2 - popup_title_w // 2, 25, popup_title, image.Color.from_rgb(255, 255, 255), popup_text_scale)
        img.draw_string(self.disp.width() // 2 - popup_content_w // 2, 202, popup_content, image.Color.from_rgb(255, 255, 255), popup_text_scale)
        img.draw_image(self.cancel_btn_x, self.cancel_btn_y, self.cancel_btn_img)
        img.draw_image(self.confirm_btn_x, self.confirm_btn_y, self.confirm_btn_img)
        img.draw_image(self.close_btn_x, self.close_btn_y, self.close_btn_img)
        if not self.time_recorded:
            self.time_initial = time.ticks_ms()
            self.time_recorded = True
    
    # 是否点击了取消
    def is_touch_cancel(self, touch_x, touch_y):
        return is_in_button(touch_x, touch_y, [self.cancel_btn_x, self.cancel_btn_y, self.cancel_btn_img.width(), self.cancel_btn_img.height()])

    # 是否点击了确认按钮
    def is_touch_confirm(self, touch_x, touch_y):
        return is_in_button(touch_x, touch_y, [self.confirm_btn_x, self.confirm_btn_y, self.confirm_btn_img.width(), self.confirm_btn_img.height()])

    # 是否点击了关闭按钮
    def is_touch_close(self, touch_x, touch_y):
        time_now = time.ticks_ms()
        print(time_now)
        print(self.time_initial)
        if time_now - self.time_initial > 1000:
            self.time_initial = time.ticks_ms()
            return is_in_button(touch_x, touch_y, [self.close_btn_x, self.close_btn_y, self.close_btn_img.width(), self.close_btn_img.height()])