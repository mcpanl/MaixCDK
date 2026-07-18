from maix import audio, app
import asyncio, websockets, json, re, time
import numpy as np

last_main_text_buffer = ""

def remove_chinese(text):
    pattern = re.compile(r'[\u4e00-\u9fa5]')  # 匹配中文字符的Unicode范围
    return pattern.sub('', text)

class AsrOnlineClient:
    def __init__(self, server_addr="localhost", server_port=6006):
        self.server_addr = server_addr
        self.server_port = server_port
        self.is_connected = False

        # 音频采集配置

        self.recorder = audio.Recorder(sample_rate=16000, channel=1, block=False)
        
        self.recorder.volume(100)
        self.recorder.reset(True)
        
        # 音频缓冲区配置
        self.audio_queue = asyncio.Queue()
        self.audio_buffer = bytes()
        self.block_size = 1600    # 50ms的音频数据量 (16000Hz * 2字节 * 0.05秒)
        self.overlap_size = 400   # 12.5ms的重叠量
        self.step_size = self.block_size - self.overlap_size  # 滑动步长

    async def inputstream_generator(self):
        """带缓冲区的音频流生成器"""
        while not app.need_exit():
            indata, status = await self.audio_queue.get()
            yield indata, status

    async def receive_results(self, socket):
        """接收识别结果"""
        global main_text_buffer, last_main_text_buffer, first_text_at
        # global speech_init_flag
        
        last_message = ""

        async for message in socket:
            # speech_init_flag = True

            if message != "Done!":
                print('enlish tts', message)
                if last_message != message:
                    last_message = message
                    if last_message:
                        obj = json.loads(last_message)
                        main_text_buffer = remove_chinese(obj["text"])
                        if not last_main_text_buffer and main_text_buffer:
                            first_text_at = time.time()
                        last_main_text_buffer = main_text_buffer
            else:
                return last_message

    async def run(self):
        """带重叠缓冲的录音核心逻辑"""
        async def record_audio():
            record_ms = 50  # 每次录音50ms以便生成重叠
            bytes_per_ms = (self.recorder.sample_rate() * self.recorder.frame_size()) // 1000
            record_bytes = record_ms * bytes_per_ms  # 12.5ms对应的字节数

            while not app.need_exit():
                while not app.need_exit():
                    remain_bytes = self.recorder.get_remaining_frames() * self.recorder.frame_size()
                    if remain_bytes >= record_bytes:
                        # 获取音频数据并添加缓冲
                        data = self.recorder.record(record_ms)
                        total_data = self.audio_buffer + data

                        # 分割带重叠的音频块
                        while not app.need_exit() and len(total_data) >= self.block_size:
                            chunk = total_data[:self.block_size]
                            await self.audio_queue.put((chunk, 0))
                            total_data = total_data[self.step_size:]  # 滑动窗口

                        self.audio_buffer = total_data  # 保存剩余数据
                    else:
                        break
                        
                    await asyncio.sleep(0.005)

                await asyncio.sleep(0.005)

        server_url = f"ws://{self.server_addr}:{self.server_port}"
        try:
            async with websockets.connect(server_url) as websocket:
                receive_task = asyncio.create_task(self.receive_results(websocket))
                print("Started! Please Speak")
                self.is_connected = True
                self.audio_task = asyncio.create_task(record_audio())

                # 发送带重叠的音频块
                async for indata, status in self.inputstream_generator():
                    if status:
                        print("Stream status:", status)
                    
                    # 转换为服务器需要的格式
                    samples_int16 = np.frombuffer(indata, dtype=np.int16)
                    samples_float32 = samples_int16.astype(np.float32) / 32768.0
                    data = samples_float32.tobytes()
                    await websocket.send(data)

                decoding_results = await receive_task
                print(f"\nFinal result:\n{decoding_results}")
        except Exception as e:
            print(f'Connection error: {e}')
        finally:
            await self.audio_task
            self.is_connected = False