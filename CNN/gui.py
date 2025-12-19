##  PC UI 코드

import argparse
import re
import sys
import time
import threading
import queue
import random

try:
    import serial  # pyserial
except Exception:
    serial = None

import tkinter as tk
from tkinter import font as tkfont

# ---------------- Board / Layout ----------------
BOARD_W   = 1060
BOARD_H   = 560
TOPBAR_H  = 44
PADDING   = 16

SIDEBAR_W = 260
LEGEND_H  = 60

SEAT_W  = 140
SEAT_H  = 100
COL_GAP = 18
ROW_GAP = 18

# --------- UPDATED: 8 seats (4x2) ----------
NUM_SEATS = 8
ROWS, COLS = 2, 4

POLL_MS = 120

STATE_NAME  = {0:"EMPTY", 1:"OCCUPIED", 2:"ONLY_BAG", 3:"TEMP_LEAVE", 4:"MISUSE"}
STATE_COLOR = {0:"#b7b7b7", 1:"#34c759", 2:"#ff9f0a", 3:"#ffd60a", 4:"#ff3b30"}
TEXT_FG     = {0:"#202020", 1:"#101010", 2:"#101010", 3:"#101010", 4:"#ffffff"}

COMPACT_FRAME_RE = re.compile(r"^[0-4]{%d}$" % NUM_SEATS)
STATUS_LINE_RE   = re.compile(
    r"Seat\s+(?P<idx>\d+)\s*:\s*state=(?P<state>\d+)\s*,\s*misuse=(?P<misuse>\d+)",
    re.IGNORECASE
)

# ---------------- 좌석 절대 좌표 정의 (auto build) ----------------
SEATS = []
start_x = PADDING
start_y = TOPBAR_H + PADDING
idx = 0
for r in range(ROWS):
    for c in range(COLS):
        x = start_x + c*(SEAT_W + COL_GAP)
        y = start_y + r*(SEAT_H + ROW_GAP)
        SEATS.append({"idx": idx, "x": x, "y": y, "w": SEAT_W, "h": SEAT_H, "label": f"Seat {idx}"})
        idx += 1

# ---------------- Serial Reader ----------------
class SerialReader(threading.Thread):
    def __init__(self, port, baud, out_q, mock=False):
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.out_q = out_q
        self.mock = mock
        self._stop = threading.Event()

    def stop(self):
        self._stop.set()

    def run(self):
        if self.mock:
            self._run_mock()
            return

        if serial is None:
            print("pyserial not available. pip install pyserial", file=sys.stderr)
            return

        try:
            with serial.Serial(self.port, self.baud, timeout=0.1) as ser:
                buf = ""
                while not self._stop.is_set():
                    try:
                        data = ser.read(256)
                        if data:
                            buf += data.decode(errors="ignore")
                            lines = buf.splitlines(keepends=False)
                            if not buf.endswith("\n") and not buf.endswith("\r"):
                                buf = lines.pop() if lines else buf
                            else:
                                buf = ""
                            for line in lines:
                                line = line.strip()
                                if line:
                                    self.out_q.put(line)
                        else:
                            time.sleep(0.02)
                    except Exception:
                        time.sleep(0.08)
        except Exception as e:
            print(f"[Serial] open error: {e}", file=sys.stderr)

    def _run_mock(self):
        states = [0]*NUM_SEATS
        t0 = time.time()
        while not self._stop.is_set():
            for i in range(NUM_SEATS):
                if random.random() < 0.10:
                    states[i] = random.randint(0,4)
            self.out_q.put("".join(str(x) for x in states))
            ms = int((time.time()-t0)*1000)
            for i in range(NUM_SEATS):
                if random.random() < 0.08:
                    self.out_q.put(f"[{ms} ms] Seat {i}: state={states[i]}, misuse={1 if states[i]==4 else 0}")
            time.sleep(0.5)

# ---------------- Model ----------------
class SeatModel:
    def __init__(self, n):
        self.state  = [0]*n
        self.misuse = [0]*n
        self.last_update = time.time()

    def apply_compact(self, frame):
        if not COMPACT_FRAME_RE.match(frame): return
        for i,ch in enumerate(frame):
            v = int(ch)
            self.state[i]  = v
            self.misuse[i] = 1 if v==4 else 0
        self.last_update = time.time()

    def apply_status_line(self, line):
        m = STATUS_LINE_RE.search(line)
        if not m: return
        i  = int(m.group("idx"))
        st = int(m.group("state"))
        ms = int(m.group("misuse"))
        if 0 <= i < len(self.state) and 0 <= st <= 4:
            self.state[i]  = st
            self.misuse[i] = 1 if st==4 else ms
            self.last_update = time.time()

# ---------------- UI ----------------
class App(tk.Tk):
    def __init__(self, model, in_q, port_text):
        super().__init__()
        self.title("Seat States Dashboard")
        self.geometry(f"{BOARD_W}x{BOARD_H}+100+80")
        self.resizable(False, False)

        self.model = model
        self.in_q  = in_q

        # 상단 바
        self.topbar = tk.Frame(self, width=BOARD_W, height=TOPBAR_H, bg="#f2f2f2", bd=0)
        self.topbar.place(x=0, y=0, width=BOARD_W, height=TOPBAR_H)
        self.topbar.pack_propagate(False)

        mono = tkfont.Font(family="Consolas" if "Consolas" in tkfont.families() else "Courier New", size=11)
        self.port_lbl  = tk.Label(self.topbar, text=f"Input: {port_text}", bg="#f2f2f2", anchor="w")
        self.clock_lbl = tk.Label(self.topbar, text="00:00:00", font=mono, width=8, bg="#f2f2f2", anchor="e")
        self.port_lbl.pack(side="left", padx=(PADDING, 8))
        self.clock_lbl.pack(side="right", padx=(8, PADDING))

        # 캔버스
        self.canvas = tk.Canvas(self, width=BOARD_W, height=BOARD_H-TOPBAR_H, bg="#fbfbfb", highlightthickness=0)
        self.canvas.place(x=0, y=TOPBAR_H)

        # 좌석 그리기
        self.items = [{} for _ in range(NUM_SEATS)]
        for s in SEATS:
            i, x, y, w, h = s["idx"], s["x"], s["y"]-TOPBAR_H, s["w"], s["h"]
            r  = self.canvas.create_rectangle(x, y, x+w, y+h, outline="#888", width=2, fill=STATE_COLOR[0])
            t1 = self.canvas.create_text(x+w/2, y+18,     text=s["label"],    font=("Segoe UI", 11, "bold"), fill=TEXT_FG[0])
            t2 = self.canvas.create_text(x+w/2, y+h/2+8,  text=STATE_NAME[0], font=("Segoe UI", 10, "bold"), fill=TEXT_FG[0])
            self.items[i] = {"rect": r, "label": t1, "state": t2}

        # 범례 + 사이드바
        self._draw_legend()
        self._build_sidebar()

        self.after(POLL_MS, self._tick)

    # ----- Legend -----
    def _draw_legend(self):
        cy0 = (BOARD_H - TOPBAR_H) - LEGEND_H + 10
        x = PADDING
        for s in range(5):
            self.canvas.create_rectangle(x, cy0, x+18, cy0+18, fill=STATE_COLOR[s], outline="#777")
            self.canvas.create_text(x+26, cy0+9, text=STATE_NAME[s], anchor="w", font=("Segoe UI", 10))
            x += 130

    # ----- Sidebar (Summary) -----
    def _build_sidebar(self):
        self.sidebar_x0 = BOARD_W - SIDEBAR_W
        self.sidebar_y0 = 0
        self.sidebar_x1 = BOARD_W
        self.sidebar_y1 = BOARD_H - TOPBAR_H

        self.canvas.create_rectangle(
            self.sidebar_x0, 0, self.sidebar_x1, self.sidebar_y1,
            fill="#f6f6f8", outline="#d0d0d0"
        )
        self.canvas.create_line(self.sidebar_x0, 0, self.sidebar_x0, self.sidebar_y1, fill="#d0d0d0")

        self.canvas.create_text(
            self.sidebar_x0 + 16, 16,
            text="Summary", anchor="nw",
            font=("Segoe UI", 12, "bold")
        )

        self.sum_items = {}
        y = 54
        row_h = 32
        for st in (1,2,3,4,0):
            box = self.canvas.create_rectangle(self.sidebar_x0 + 16, y, self.sidebar_x0 + 34, y+18,
                                               fill=STATE_COLOR[st], outline="#999")
            lbl = self.canvas.create_text(self.sidebar_x0 + 40, y+9, text=STATE_NAME[st],
                                          anchor="w", font=("Segoe UI", 10, "bold"))
            cnt = self.canvas.create_text(self.sidebar_x1 - 18, y+9, text="0",
                                          anchor="e", font=("Segoe UI", 10))
            lst = self.canvas.create_text(self.sidebar_x0 + 16, y+22, text="",
                                          anchor="nw", font=("Segoe UI", 9),
                                          width=SIDEBAR_W - 32)
            self.sum_items[st] = {"box": box, "label": lbl, "count": cnt, "list": lst}
            y += row_h + 36

    # ----- Tick -----
    def _tick(self):
        while True:
            try:
                line = self.in_q.get_nowait()
            except queue.Empty:
                break
            if COMPACT_FRAME_RE.match(line):
                self.model.apply_compact(line)
            else:
                self.model.apply_status_line(line)

        self._render()
        self._render_summary()

        self.clock_lbl.config(text=time.strftime("%H:%M:%S"))
        self.after(POLL_MS, self._tick)

    def _render(self):
        for s in SEATS:
            i = s["idx"]
            st = self.model.state[i]
            bg = STATE_COLOR.get(st, "#ddd")
            fg = TEXT_FG.get(st, "#101010")

            it = self.items[i]
            self.canvas.itemconfigure(it["rect"], fill=bg)
            self.canvas.itemconfigure(it["label"], fill=fg)
            self.canvas.itemconfigure(it["state"], text=STATE_NAME.get(st,"?"), fill=fg)

    def _render_summary(self):
        buckets = {0:[], 1:[], 2:[], 3:[], 4:[]}
        for i, st in enumerate(self.model.state):
            buckets[st].append(i)

        for st, item in self.sum_items.items():
            seats = buckets[st]
            self.canvas.itemconfigure(item["count"], text=str(len(seats)))
            s = "Seats: " + (", ".join(map(str, seats)) if seats else "-")
            self.canvas.itemconfigure(item["list"], text=s)

# ---------------- Main ----------------
def main():
    parser = argparse.ArgumentParser(description="Seat dashboard (8 seats)")
    parser.add_argument("--port", help="Serial port (e.g., COM14)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--mock", action="store_true", help="Run without serial")
    args = parser.parse_args()

    in_q  = queue.Queue()
    model = SeatModel(NUM_SEATS)

    port_text = "Mock mode" if args.mock else f"{args.port} @ {args.baud}"
    reader = SerialReader(args.port, args.baud, in_q, mock=args.mock)
    reader.start()

    app = App(model, in_q, port_text)
    try:
        app.mainloop()
    finally:
        reader.stop()

if __name__ == "__main__":
    main()

