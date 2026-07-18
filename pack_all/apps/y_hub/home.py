from maix import image
from base_page import BasePage

class HomePage(BasePage):
    collect_image_postion = [0,0,1,1]
    collect_image = None
    deploy_image_postion = [0,0,1,1]
    deploy_image = None

    def __init__(self, name, disp, cam, cam_min):
        super().__init__(name, disp, cam, cam_min)

        self.collect_image = image.load("./images/CollectImage.png", format=image.Format.FMT_BGRA8888)
        self.deploy_image = image.load("./images/DeployModel.png", format=image.Format.FMT_BGRA8888)
        self.collect_image_postion = [
            self.disp.width() // 2  - self.collect_image.width() // 2,
            self.disp.height() // 2 - self.collect_image.height() // 2 - self.collect_image.height() // 2 - 18,
            self.collect_image.width(),
            self.collect_image.height()
        ]
        self.deploy_image_postion = [
            self.disp.width() // 2  - self.deploy_image.width() // 2,
            self.disp.height() // 2 - self.deploy_image.height() // 2 + self.deploy_image.height() // 2 + 18,
            self.deploy_image.width(),
            self.deploy_image.height()
        ]
        

    def show(self):
        print("SHOW")
        self.active = True

    def hide(self):
        print("HIDE")
        self.active = False

    def hanlder_touch_pressed(self, x, y):
        if self.is_in_button(x, y, self.collect_image_postion):
            self.emit_event('on_home_button_click', "collect")
        elif self.is_in_button(x, y, self.deploy_image_postion):
            self.emit_event('on_home_button_click', "deploy")

    def loop(self, img, ts_res):

        # img.draw_rect(5, 5, 55, 55, image.COLOR_BLUE)

        img.draw_image(
            self.collect_image_postion[0],
            self.collect_image_postion[1],
            self.collect_image
        )

        img.draw_image(
            self.deploy_image_postion[0],
            self.deploy_image_postion[1],
            self.deploy_image
        )

        return img


    def init(self):
        print("INIT")