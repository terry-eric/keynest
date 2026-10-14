"""Validated macro input. Importing this module never injects keyboard input."""
import os
import re
import shutil
import subprocess
import sys
import time

KEYS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789`-=[]\\;',./") | {f"F{i}" for i in range(1, 25)} | set(
    "CTRL SHIFT ALT META RCTRL RSHIFT RALT RMETA ENTER ESC BACKSPACE TAB SPACE CAPSLOCK INSERT HOME PAGEUP DELETE END PAGEDOWN LEFT RIGHT UP DOWN PRINTSCREEN SCROLLLOCK PAUSE NUMLOCK VOLUMEUP VOLUMEDOWN MUTE PLAYPAUSE NEXT PREVIOUS".split())
SPECIAL = dict(zip("CTRL SHIFT ALT META RCTRL RSHIFT RALT RMETA ENTER ESC BACKSPACE TAB SPACE CAPSLOCK INSERT HOME PAGEUP DELETE END PAGEDOWN LEFT RIGHT UP DOWN PRINTSCREEN SCROLLLOCK PAUSE NUMLOCK VOLUMEUP VOLUMEDOWN MUTE PLAYPAUSE NEXT PREVIOUS".split(),
                   "ctrl_l shift_l alt_l cmd_l ctrl_r shift_r alt_r cmd_r enter esc backspace tab space caps_lock insert home page_up delete end page_down left right up down print_screen scroll_lock pause num_lock media_volume_up media_volume_down media_volume_mute media_play_pause media_next media_previous".split()))

def validate_program(program):
    import json
    if not isinstance(program, dict) or set(program) != {"name", "steps"}:
        raise ValueError("Invalid macro fields")
    def text(value, maximum, empty=False):
        if not isinstance(value, str) or (not value and not empty) or len(value.encode("utf-8")) > maximum or re.search(r"[\x00-\x08\x0b-\x1f\x7f]", value):
            raise ValueError("Invalid macro text")
    text(program["name"], 64, True)
    steps = program["steps"]
    if not isinstance(steps, list) or len(steps) > 16:
        raise ValueError("Invalid macro step count")
    delay = 0
    for step in steps:
        if not isinstance(step, dict): raise ValueError("Invalid macro step")
        if step.get("type") == "keys" and set(step) == {"type", "keys"}:
            keys = step["keys"]
            if not isinstance(keys, list) or not 1 <= len(keys) <= 16 or any(not isinstance(key, str) or key not in KEYS for key in keys) or len(set(keys)) != len(keys):
                raise ValueError("Invalid macro chord")
        elif step.get("type") == "text" and set(step) == {"type", "text"}: text(step["text"], 512)
        elif step.get("type") == "wait" and set(step) == {"type", "ms"}:
            if type(step["ms"]) is not int or not 0 <= step["ms"] <= 5000: raise ValueError("Invalid macro delay")
            delay += step["ms"]
        else: raise ValueError("Invalid macro action")
    if delay > 30000 or len(json.dumps(program, ensure_ascii=False, separators=(",", ":")).encode()) > 1200:
        raise ValueError("Macro exceeds limits")
    return program

class NativeInput:
    def __init__(self):
        self.wayland = bool(os.environ.get("WAYLAND_DISPLAY"))
        if self.wayland:
            if not shutil.which("wtype"): raise RuntimeError("Wayland 需要 wtype，且桌面需支援 virtual-keyboard 協定。")
            self.controller = None
        else:
            try:
                from pynput.keyboard import Controller, Key, Listener
                if sys.platform == "darwin" and not Listener.IS_TRUSTED:
                    raise RuntimeError("macOS 尚未授予輔助使用權限；請在系統設定允許執行 Python 的程式，再重啟工具。")
                self.controller, self.Key = Controller(), Key
            except (ImportError, OSError) as error:
                raise RuntimeError("請安裝 requirements-companion.txt；macOS 需要輔助使用權限，Linux 需要桌面工作階段。") from error

    def resolve(self, name):
        if name in SPECIAL: return getattr(self.Key, SPECIAL[name])
        if re.fullmatch(r"F(?:[1-9]|1[0-9]|2[0-4])", name): return getattr(self.Key, name.lower())
        return name.lower()

    def prepare(self, program):
        validate_program(program)
        # Resolve the whole program before any input; unsupported keys never partially execute it.
        if not self.wayland:
            for step in program["steps"]:
                if step["type"] == "keys":
                    for name in step["keys"]: self.resolve(name)

    def type_text(self, text):
        if self.wayland:
            # Stdin keeps private text out of process arguments and treats '-' safely.
            subprocess.run(["wtype", "-d", "5", "-"], input=text.encode(), check=True, timeout=30, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            for character in text:
                if character in "\n\t":
                    key=self.Key.enter if character == "\n" else self.Key.tab
                    self.controller.press(key); self.controller.release(key)
                else: self.controller.type(character)

    def chord(self, names):
        if self.wayland:
            modifiers={"CTRL":"ctrl","SHIFT":"shift","ALT":"alt","META":"logo","RCTRL":"ctrl","RSHIFT":"shift","RALT":"alt","RMETA":"logo"}
            symbols={"ENTER":"Return","ESC":"Escape","BACKSPACE":"BackSpace","TAB":"Tab","SPACE":"space","CAPSLOCK":"Caps_Lock","PAGEUP":"Page_Up","PAGEDOWN":"Page_Down","PRINTSCREEN":"Print","SCROLLLOCK":"Scroll_Lock","NUMLOCK":"Num_Lock","VOLUMEUP":"XF86AudioRaiseVolume","VOLUMEDOWN":"XF86AudioLowerVolume","MUTE":"XF86AudioMute","PLAYPAUSE":"XF86AudioPlay","NEXT":"XF86AudioNext","PREVIOUS":"XF86AudioPrev"}
            arguments=["wtype"]
            for name in names: arguments.extend(["-M",modifiers[name]] if name in modifiers else ["-P",symbols.get(name,name.lower() if len(name)==1 else name.title() if name in SPECIAL else name)])
            arguments += ["-s", "50"]
            for name in reversed(names): arguments.extend(["-m",modifiers[name]] if name in modifiers else ["-p",symbols.get(name,name.lower() if len(name)==1 else name.title() if name in SPECIAL else name)])
            subprocess.run(arguments, check=True, timeout=10, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            held=[]
            try:
                for name in names:
                    key=self.resolve(name); self.controller.press(key); held.append(key)
                time.sleep(.05)
            finally:
                failure = None
                for key in reversed(held):
                    try: self.controller.release(key)
                    except Exception as error: failure = failure or error
                if failure: raise failure

def execute(program, backend, closing, sleep=None):
    validate_program(program)
    backend.prepare(program)
    wait = sleep or closing.wait
    for step in program["steps"]:
        if closing.is_set(): return
        if step["type"] == "wait":
            if wait(step["ms"] / 1000): return
        elif step["type"] == "text": backend.type_text(step["text"])
        else: backend.chord(step["keys"])
