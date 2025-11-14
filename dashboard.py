import tkinter as tk
from tkinter import ttk
import json
import os
import subprocess
import signal

NODE_STATUS_FILE = "node_status.json"
CLIENT_BINARY = "./grpc-server-client/cpp_client/build/client"
REFRESH_INTERVAL_MS = 2000  # 2 seconds

class NodeWidget(tk.Frame):
    def __init__(self, master, node_id, node_num, spawn_callback, kill_callback):
        super().__init__(master)
        self.node_id = node_id
        self.node_num = node_num
        self.spawn_callback = spawn_callback
        self.kill_callback = kill_callback

        self.led_canvas = tk.Canvas(self, width=20, height=20, highlightthickness=0)
        self.led = self.led_canvas.create_oval(2, 2, 18, 18, fill="red")
        self.led_canvas.grid(row=0, column=0, padx=5, pady=5)

        self.label = tk.Label(self, text=node_id, font=("Arial", 10, "bold"))
        self.label.grid(row=0, column=1, padx=5)

        self.downtime_label = tk.Label(self, text="Downtime: 0s", font=("Arial", 9))
        self.downtime_label.grid(row=1, column=0, columnspan=2)

        self.switch_var = tk.BooleanVar()
        self.switch = ttk.Checkbutton(self, text="Spawn/Kill", variable=self.switch_var,
                                      command=self.on_switch)
        self.switch.grid(row=2, column=0, columnspan=2, pady=2)

    def update(self, status, downtime):
        color = "green" if status == "online" else "red"
        self.led_canvas.itemconfig(self.led, fill=color)
        self.downtime_label.config(text=f"Downtime: {downtime}s")

    def on_switch(self):
        if self.switch_var.get():
            self.spawn_callback(self.node_num)
        else:
            self.kill_callback(self.node_num)

class Dashboard(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Node Health Dashboard")
        self.geometry("700x350")
        self.resizable(False, False)

        self.node_widgets = {}
        self.client_processes = {}  # node_num: subprocess.Popen
        self.create_ui()
        self.after(REFRESH_INTERVAL_MS, self.refresh_status)

    def create_ui(self):
        frame = tk.Frame(self)
        frame.pack(pady=20)

        for i in range(10):
            node_id = f"node-{i+1:02d}"
            node_num = i + 1
            row = 0 if i < 5 else 1
            col = i if i < 5 else i - 5
            widget = NodeWidget(frame, node_id, node_num,
                               self.spawn_client, self.kill_client)
            widget.grid(row=row, column=col, padx=8, pady=10)
            self.node_widgets[node_id] = widget

    def refresh_status(self):
        if os.path.exists(NODE_STATUS_FILE):
            try:
                with open(NODE_STATUS_FILE, "r") as f:
                    data = json.load(f)
                for node_id, widget in self.node_widgets.items():
                    node_info = data.get(node_id, {})
                    status = node_info.get("status", "offline")
                    downtime = node_info.get("total_downtime", 0)
                    widget.update(status, downtime)
            except Exception as e:
                print("Error reading node_status.json:", e)
        self.after(REFRESH_INTERVAL_MS, self.refresh_status)

    def spawn_client(self, node_num):
        if node_num in self.client_processes and self.client_processes[node_num].poll() is None:
            # Already running
            return
        try:
            proc = subprocess.Popen([CLIENT_BINARY, str(node_num)])
            self.client_processes[node_num] = proc
            print(f"Spawned client {node_num}")
        except Exception as e:
            print(f"Failed to spawn client {node_num}: {e}")

    def kill_client(self, node_num):
        proc = self.client_processes.get(node_num)
        if proc and proc.poll() is None:
            try:
                proc.terminate()
                proc.wait(timeout=5)
                print(f"Killed client {node_num}")
            except Exception:
                proc.kill()
                print(f"Force killed client {node_num}")
        self.client_processes.pop(node_num, None)

if __name__ == "__main__":
    Dashboard().mainloop()