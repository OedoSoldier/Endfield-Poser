"""A bounded local mailbox. This module never imports or accesses bpy."""
import json
import queue
import threading
import urllib.request
import urllib.error


class Client:
    def __init__(self, port=18923):
        self.url = f"http://127.0.0.1:{port}/api/blender/"
        self.commands = queue.Queue(maxsize=8)
        self.results = queue.Queue()
        self.lock = threading.Lock()
        self.latest = None
        self.heartbeat = None
        self.closed = threading.Event()
        self.thread = threading.Thread(target=self._run, name="Endfield bridge", daemon=True)
        self.thread.start()

    def request(self, path, data, callback=None):
        task = (path, data, callback)
        if path == 'ping':
            # Idle previews must not fill the command queue while the game is
            # slow/in the background. A newer heartbeat replaces the old one.
            with self.lock:
                self.heartbeat = task
            return
        if path == 'end':
            with self.lock:
                self.latest = self.heartbeat = None
            while True:
                try:
                    self.commands.get_nowait()
                except queue.Empty:
                    break
        try:
            self.commands.put_nowait(task)
        except queue.Full:
            raise RuntimeError('游戏正在处理上一项操作，请稍后重试') from None

    def preview(self, data, callback=None):
        with self.lock:
            self.latest = ("frame", data, callback)
            self.heartbeat = None

    def _run(self):
        # Do not let environment HTTP proxies redirect loopback game traffic.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        while not self.closed.is_set():
            try:
                task = self.commands.get(timeout=0.015)
            except queue.Empty:
                with self.lock:
                    task, self.latest = self.latest, None
                    if task is None:
                        task, self.heartbeat = self.heartbeat, None
                if task is None:
                    continue
            path, data, callback = task
            try:
                request = urllib.request.Request(self.url + path,
                    data=json.dumps(data, ensure_ascii=False, allow_nan=False).encode("utf-8"),
                    headers={"Content-Type": "application/json"}, method="POST")
                try:
                    response = opener.open(request, timeout=3)
                except urllib.error.HTTPError as error:
                    response = error  # Keep the game's actual error message.
                with response:
                    raw = response.read(4 * 1024 * 1024 + 1)
                if len(raw) > 4 * 1024 * 1024:
                    raise ValueError("游戏回复超出大小限制")
                result = json.loads(raw)
            except Exception as exc:
                result = {"ok": False, "error": str(exc), "retryable": isinstance(exc, (OSError, TimeoutError, urllib.error.URLError))}
            if callback:
                self.results.put((callback, result))

    def poll(self):
        for _ in range(32):
            try:
                callback, result = self.results.get_nowait()
            except queue.Empty:
                break
            callback(result)

    def close(self):
        self.closed.set()
        with self.lock:
            self.latest = None
            self.heartbeat = None
