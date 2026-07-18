from maix import image
from base_page import BasePage
from delete_popup import DeletePopup

class ModelItemPage(BasePage):
    model = None
    delete_image_postion = [0,0,1,1]
    delete_image = None
    deploy_image_postion = [0,0,1,1]
    deploy_image = None
    def __init__(self, name, disp, cam, cam_min):
        super().__init__(name, disp, cam, cam_min)
        
        self.delete_popup = DeletePopup(disp) # 删除确认弹窗
        self.show_delete_popup = False
        self.delete_image = image.load("./images/DeleteModel.png", format=image.Format.FMT_BGRA8888)
        self.deploy_image = image.load("./images/DeployModelActive.png", format=image.Format.FMT_BGRA8888)
        self.delete_image_postion = [
            self.disp.width() // 2  - self.delete_image.width() // 2,
            self.disp.height() // 2 - self.delete_image.height() // 2 + self.delete_image.height() // 2 + 18,
            self.delete_image.width(),
            self.delete_image.height()
        ]
        self.deploy_image_postion = [
            self.disp.width() // 2  - self.deploy_image.width() // 2,
            self.disp.height() // 2 - self.deploy_image.height() // 2 - self.deploy_image.height() // 2 - 18,
            self.deploy_image.width(),
            self.deploy_image.height()
        ]
        
    def show(self):
        print("Item SHOW")
        self.active = True

    def hide(self):
        print("Item HIDE")
        self.active = False

    def hanlder_touch_pressed(self, x, y):
        if self.show_delete_popup == True:
            # 如果点击删除弹窗的取消按钮或关闭按钮
            if self.delete_popup.is_touch_cancel(x, y) or self.delete_popup.is_touch_close(x, y):
                self.show_delete_popup = False
                self.continue_base_hanlder_touch_pressed = False
            # 如果点击删除弹窗的确认按钮
            elif self.delete_popup.is_touch_confirm(x, y):
                self.show_delete_popup = False
                self.emit_event('on_model_item_button_click', "delete")
                self.continue_base_hanlder_touch_pressed = False
        else:
            if self.is_in_button(x, y, self.delete_image_postion):
                self.show_delete_popup = True
                self.continue_base_hanlder_touch_pressed = True
            elif self.is_in_button(x, y, self.deploy_image_postion):
                self.emit_event('on_model_item_button_click', "deploy")

    def loop(self, img, ts_res):
        img.draw_image(
            self.delete_image_postion[0],
            self.delete_image_postion[1],
            self.delete_image
        )

        img.draw_image(
            self.deploy_image_postion[0],
            self.deploy_image_postion[1],
            self.deploy_image
        )

        if self.show_delete_popup == True:
            self.delete_popup.draw(img)

        return img

    def init(self):
        print("Item INIT")