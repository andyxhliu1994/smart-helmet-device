import asyncio
import websockets

# 定义处理客户端连接的函数
async def echo_server(websocket, path):
    print("有新的客户端连接成功！")
    try:
        # 持续循环，保持与客户端的通信
        async for message in websocket:
            print(f"{path}:{message}")
            # 可选：向客户端发送回复
            if path == "/rv1126b/cmd":
                await websocket.send(f"服务端已收到你的消息: {message}")
            elif path == "/rv1126b/voice":
                file_path = "output.pcm"
                try:
                    # 以二进制只读模式打开文件
                    with open(file_path, "rb") as f:
                        # 读取文件内容
                        file_content = f.read()
                        # 发送二进制数据
                        await websocket.send(file_content)
                        print("二进制文件已成功发送！")
                except FileNotFoundError:
                    print(f"错误：文件 {file_path} 不存在。")
            
    except websockets.ConnectionClosed:
        print("客户端断开了连接")
    except Exception as e:
        print(f"发生错误: {e}")

# 启动 WebSocket 服务端
async def main():
    async with websockets.serve(echo_server, "0.0.0.0", 9000):
        print("WebSocket 服务端已启动，正在监听 ws://0.0.0.0:9000")
        await asyncio.Future()  # 运行无限期，保持服务器开启

if __name__ == "__main__":
    asyncio.run(main())
