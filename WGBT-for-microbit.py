from microbit import *
import utime

uart.init(baudrate=115200, tx=pin16, rx=pin2)

# ★ 顔データ（あなたのUIを維持）
danger_face = Image("90009:09090:00000:99999:90909")
severe_face = Image("00000:09090:00000:09090:90909")
alert_face  = Image("00000:09090:00000:09990:90009")
caution_face= Image("00000:09090:00000:90009:09990")

caution2 = Image("00000:99099:00000:09990:00000")
alert_close = Image("00000:99099:00000:09990:00000")
severe_right = Image("00000:99099:00000:09990:00000")
danger_blank = Image("00000:99099:00000:09990:00000")


def show_caution():
    display.show([caution2, caution_face], delay=400)

def show_alert():
    display.show([alert_close, alert_face], delay=260)

def show_severe():
    display.show([severe_right, severe_face], delay=150)

def show_danger():
    display.show([danger_blank, danger_face], delay=70)

# ★ キャッシュ
today_wbgt = None
tomorrow_wbgt = None

# ★ 壊れない UART 行単位読み
def uart_readline():
    buf = b""
    timeout = utime.ticks_ms()
    while True:
        if uart.any():
            c = uart.read(1)
            if c:
                buf += c
                if c == b"\n":
                    break
        if utime.ticks_diff(utime.ticks_ms(), timeout) > 300:
            break
    return buf.decode().strip()

# ★ 危険度判定
def get_level(wbgt):
    if wbgt >= 31:
        return "danger"
    elif wbgt >= 28:
        return "severe"
    elif wbgt >= 25:
        return "alert"
    else:
        return "caution"

# ★ 危険度アニメーション
def play_animation(level):
    if level == "danger":
        show_danger()
    elif level == "severe":
        show_severe()
    elif level == "alert":
        show_alert()
    else:
        show_caution()

# ★ 前置き表示（TODAY / TMR）
def prefix(text):
    display.scroll(text, delay=80)
    utime.sleep(0.1)

anim_count = 0

while True:

    # ★ キャッシュが揃っていない時だけ REQ を送る
    if today_wbgt is None or tomorrow_wbgt is None:
        uart.write("REQ\n")
        display.scroll("WAIT")
        utime.sleep(1)

    # ★ ESPr からのデータ受信
    line = uart_readline()
    if line != "":
        if line.startswith("WBGT:"):
            try:
                today_wbgt = int(line.split(":")[1])
                print("TODAY =", today_wbgt)
            except:
                pass

        elif line.startswith("TWBGT:"):
            try:
                tomorrow_wbgt = int(line.split(":")[1])
                print("TOMORROW =", tomorrow_wbgt)
            except:
                pass

    # ★ キャッシュが揃っていない場合は WAIT 継続
    if today_wbgt is None or tomorrow_wbgt is None:
        continue

    # ★ Aボタン → 明日の WBGT（前置き → 数字 → アニメーション）
    tommrrow_anim_count = 0
    if button_a.was_pressed():

        prefix("TMR")                     # ← ★ アニメーションの前に TMR を表示
        display.scroll(str(tomorrow_wbgt))
        utime.sleep(0.15)
        anim_count = 0
        while tommrrow_anim_count <= 5:
            play_animation(get_level(tomorrow_wbgt))
            tommrrow_anim_count += 1
            utime.sleep(0.1)

    else:
        # ★ 通常 → 今日の WBGT（前置き → 数字 → アニメーション）
        wbgt = today_wbgt
        level = get_level(wbgt)

        if anim_count >= 3:
            # prefix("TODAY")               # ← ★ アニメーションの前に TODAY を表示
            display.scroll(str(wbgt))
            utime.sleep(0.15) 
            anim_count = 0
        else:
            play_animation(level)
            utime.sleep(0.15) 
            anim_count += 1

    utime.sleep(0.1)
