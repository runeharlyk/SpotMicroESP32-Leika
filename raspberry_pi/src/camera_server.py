from http.server import BaseHTTPRequestHandler, HTTPServer
from threading import Thread
from multiprocessing import Process
import cv2
import time


class StreamingHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/stream.mjpg':
            self.send_response(200)
            self.send_header('Content-type', 'multipart/x-mixed-replace; boundary=frame')
            self.end_headers()
            try:
                while True:
                    if not self.server.frame_queue.empty():
                        frame = self.server.frame_queue.get()

                        _, jpeg = cv2.imencode('.jpg', frame)
                        self.wfile.write(b'--frame\r\n')
                        self.send_header('Content-Type', 'image/jpeg')
                        self.send_header('Content-Length', len(jpeg))
                        self.end_headers()
                        self.wfile.write(jpeg.tobytes())
                        self.wfile.write(b'\r\n')
                    time.sleep(0.1)
            except Exception as e:
                print(f"Stream stopped: {e}")
                raise e
        else:
            self.send_error(404)
            self.end_headers()

class StreamingServer(HTTPServer):
    def __init__(self, server_address, frame_queue):
        super().__init__(server_address, StreamingHandler)
        self.frame_queue = frame_queue

class StreamingServerThread:
    def __init__(self, frame_queue, port=8080):
        self.frame_queue = frame_queue
        self.port = port
        self.server_thread = None

    def run_server(self, frame_queue):
        address = ('', self.port)
        server = StreamingServer(address, frame_queue)
        print(f"Starting server at http://localhost:{self.port}/stream.mjpg")
        server.serve_forever()

    def start(self):
        self.server_thread = Thread(target=self.run_server)
        self.server_thread.daemon = True
        self.server_thread.start()

    def start_process(self):
        self.server_process = Process(target=self.run_server, args=(self.frame_queue,))
        self.server_process.start()

    def stop(self):
        if self.server_thread:
            self.server_thread.join()
        if self.server_process:
            self.server_process.join()
