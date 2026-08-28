import os
import sys
import subprocess
import re
from PIL import Image, ImageDraw, ImageFont

ANSI_COLOR_MAP = {
    '0': (201, 209, 217),      # default fg / reset
    '1': (240, 246, 252),      # bold / bright white
    '30': (72, 79, 88),        # black
    '31': (248, 81, 73),       # red
    '32': (126, 231, 135),     # green
    '33': (210, 153, 34),      # yellow
    '34': (88, 166, 255),      # blue
    '35': (210, 168, 255),     # magenta
    '36': (88, 166, 255),      # cyan
    '37': (240, 246, 252),     # white
    '1;31': (248, 81, 73),
    '1;32': (126, 231, 135),
    '1;33': (210, 153, 34),
    '1;34': (88, 166, 255),
    '1;36': (121, 192, 255),
    '1;37': (240, 246, 252),
}

def parse_ansi(text):
    tokens = []
    pattern = re.compile(r'\033\[([0-9;]+)m')
    pos = 0
    current_color = ANSI_COLOR_MAP['0']

    for match in pattern.finditer(text):
        if match.start() > pos:
            tokens.append((text[pos:match.start()], current_color))
        code = match.group(1)
        current_color = ANSI_COLOR_MAP.get(code, ANSI_COLOR_MAP['0'])
        pos = match.end()

    if pos < len(text):
        tokens.append((text[pos:], current_color))
    return tokens

def render_terminal_frame(lines_to_draw, prompt_text="user@workstation:~/memsentry$ ./bin/demo", width=960, height=580):
    img = Image.new("RGBA", (width, height), (13, 17, 23, 255))
    draw = ImageDraw.Draw(img)

    # Window Header Bar
    draw.rectangle([(0, 0), (width, 38)], fill=(22, 27, 34, 255))
    draw.line([(0, 38), (width, 38)], fill=(48, 54, 61, 255), width=1)

    # Window Buttons
    draw.ellipse([(18, 13), (28, 23)], fill=(255, 95, 86, 255))
    draw.ellipse([(36, 13), (46, 23)], fill=(255, 189, 46, 255))
    draw.ellipse([(54, 13), (64, 23)], fill=(39, 201, 63, 255))

    # Font
    font_path = "C:/Windows/Fonts/consola.ttf"
    font = ImageFont.truetype(font_path, 13) if os.path.exists(font_path) else ImageFont.load_default()
    title_font = ImageFont.truetype(font_path, 12) if os.path.exists(font_path) else ImageFont.load_default()

    # Title
    draw.text((width // 2 - 100, 12), "memsentry — live audit demo", fill=(139, 148, 158, 255), font=title_font)

    # Draw Prompt
    y = 52
    draw.text((24, y), "user@workstation", fill=(126, 231, 135, 255), font=font)
    draw.text((145, y), ":", fill=(139, 148, 158, 255), font=font)
    draw.text((153, y), "~/memsentry", fill=(88, 166, 255, 255), font=font)
    draw.text((235, y), "$", fill=(201, 209, 217, 255), font=font)
    draw.text((248, y), prompt_text.split("$ ")[-1] if "$ " in prompt_text else prompt_text, fill=(240, 246, 252, 255), font=font)
    y += 22

    # Draw Output Lines
    for line in lines_to_draw:
        tokens = parse_ansi(line)
        x = 24
        for chunk, color in tokens:
            draw.text((x, y), chunk, fill=color, font=font)
            bbox = font.getbbox(chunk)
            x += (bbox[2] - bbox[0])
        y += 18
        if y > height - 20:
            break

    return img

def main():
    exe_path = os.path.abspath("bin/demo.exe")
    if not os.path.exists(exe_path):
        print(f"[-] Executable not found at {exe_path}. Build it first.")
        sys.exit(1)

    print(f"[*] Running real executable: {exe_path}...")
    res = subprocess.run([exe_path], capture_output=True, text=True, encoding='utf-8')
    raw_output = res.stdout
    print(f"[+] Output captured ({len(raw_output)} chars).")

    lines = raw_output.splitlines()
    frames = []

    # 1. Typing animation for command
    full_cmd = "./bin/demo"
    for i in range(1, len(full_cmd) + 1):
        frame = render_terminal_frame([], prompt_text=f"user@workstation:~/memsentry$ {full_cmd[:i]}")
        frames.append((frame.convert("RGB"), 60))

    # 2. Progressive output reveal
    step = 2
    for i in range(1, len(lines) + 1, step):
        current_lines = lines[:i]
        frame = render_terminal_frame(current_lines, prompt_text=f"user@workstation:~/memsentry$ {full_cmd}")
        frames.append((frame.convert("RGB"), 120))

    # Final full frame
    final_frame = render_terminal_frame(lines, prompt_text=f"user@workstation:~/memsentry$ {full_cmd}")
    # Hold final frame for 6 seconds (6000ms)
    frames.append((final_frame.convert("RGB"), 6000))

    out_gif = os.path.abspath("assets/demo.gif")
    os.makedirs(os.path.dirname(out_gif), exist_ok=True)

    imgs = [f[0] for f in frames]
    durations = [f[1] for f in frames]

    print(f"[*] Compiling {len(imgs)} frames to {out_gif}...")
    imgs[0].save(
        out_gif,
        save_all=True,
        append_images=imgs[1:],
        duration=durations,
        loop=0,
        optimize=True
    )
    print(f"[SUCCESS] Demo GIF generated successfully at: {out_gif}")

if __name__ == '__main__':
    main()
