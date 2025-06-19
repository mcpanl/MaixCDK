launcher
====

要启动程序，退出时写入 `/tmp/run_app.txt`
* 第一行： app 可执行文件路径，如果是 python 文件，则 `/**/**/main.py`
* 第二行： app_id
* 第三行： 传给 app 的参数 start_param，字符串类型

程序可以调用 `maix.app.switch_app()` 函数切换应用，以上行为封装在里面了。
被启动的程序可以通过`maix.app.get_start_param()`函数获得传参。
