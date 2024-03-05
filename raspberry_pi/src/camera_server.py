from http.server import BaseHTTPRequestHandler, HTTPServer
from threading import Thread
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
                    frame = self.server.camera.get_image()
                    _, jpeg = cv2.imencode('.jpg', frame)
                    self.wfile.write(b'--frame\r\n')
                    self.send_header('Content-Type', 'image/jpeg')
                    self.send_header('Content-Length', len(jpeg))
                    self.end_headers()
                    self.wfile.write(jpeg.tobytes())
                    self.wfile.write(b'\r\n')
                    time.sleep(0.5)
            except Exception as e:
                print(f"Stream stopped: {e}")
        else:
            self.send_error(404)
            self.end_headers()

class StreamingServer(HTTPServer):
    def __init__(self, server_address, camera):
        super().__init__(server_address, StreamingHandler)
        self.camera = camera

class StreamingServerThread:
    def __init__(self, camera, port=8080):
        self.camera = camera
        self.port = port
        self.server_thread = None

    def start(self):
        def run_server():
            address = ('', self.port)
            server = StreamingServer(address, self.camera)
            print(f"Starting server at http://localhost:{self.port}/stream.mjpg")
            server.serve_forever()

        self.server_thread = Thread(target=run_server)
        self.server_thread.daemon = True
        self.server_thread.start()

