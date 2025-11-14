import tkinter as tk
from tkinter import ttk
import json
import os

NODE_STATUS_FILE = "../node_status.json"
REFRESH_INTERVAL_MS = 2000  # 2 seconds

class NodeWidget(tk.Frame):
    def __init__(self, master, node_id):
        super().__init__(master)
        self.node_id = node_id

        self.led_canvas = tk.Canvas(self, width=20, height=20, highlightthickness=0)
        self.led = self.led_canvas.create_oval(2, 2, 18, 18, fill="red")
        self.led_canvas.grid(row=0, column=0, padx=5, pady=5)

        self.label = tk.Label(self, text=node_id, font=("Arial", 10, "bold"))
        self.label.grid(row=0, column=1, padx=5)

        self.downtime_label = tk.Label(self, text="Downtime: 0s", font=("Arial", 9))
        self.downtime_label.grid(row=1, column=0, columnspan=2)

        self.switch_var = tk.BooleanVar()
        self.switch = ttk.Checkbutton(self, text="Spawn/Kill", variable=self.switch_var)
        self.switch.grid(row=2, column=0, columnspan=2, pady=2)

    def update(self, status, downtime):
        color = "green" if status == "online" else "red"
        self.led_canvas.itemconfig(self.led, fill=color)
        self.downtime_label.config(text=f"Downtime: {downtime}s")

class Dashboard(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Node Health Dashboard")
        self.geometry("700x350")
        self.resizable(False, False)

        self.node_widgets = {}
        self.create_ui()
        self.after(REFRESH_INTERVAL_MS, self.refresh_status)

    def create_ui(self):
        frame = tk.Frame(self)
        frame.pack(pady=20)

        for i in range(10):
            node_id = f"node-{i+1:02d}"
            row = 0 if i < 5 else 1
            col = i if i < 5 else i - 5
            widget = NodeWidget(frame, node_id)
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

if __name__ == "__main__":
    Dashboard().mainloop()