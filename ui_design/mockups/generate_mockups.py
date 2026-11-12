#!/usr/bin/env python3
"""
DSO183 UI Mockup Generator - Dark Blue Instrumentation Theme
Generates pixel-perfect 320x240 bitmaps and 4x presentation renders for the
new retro laboratory oscilloscope user interface design.
"""

import os
import math
from PIL import Image, ImageDraw

# -------------------------------------------------------------------------
# Exact 5x7 GLCD Font Data (Parsed directly from fonts/glcdfont.c)
# -------------------------------------------------------------------------
def load_glcd_font():
    font_path = os.path.join(os.path.dirname(__file__), '../../fonts/glcdfont.c')
    if not os.path.exists(font_path):
        font_path = 'fonts/glcdfont.c'
    
    font_bytes = bytearray()
    with open(font_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()
            if line.startswith('0x'):
                parts = line.split(',')
                for p in parts:
                    p = p.strip()
                    if p.startswith('0x'):
                        font_bytes.append(int(p[:4], 16))
    return font_bytes

GLCD_BYTES = load_glcd_font()

# -------------------------------------------------------------------------
# Color Palette (Darkish Blues Instrumentation Theme in RGB565)
# -------------------------------------------------------------------------
def rgb565_quantize(r, g, b):
    """Quantize 24-bit RGB to 16-bit RGB565 and back to 24-bit RGB."""
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    r_out = (r5 * 527 + 23) >> 6
    g_out = (g6 * 259 + 33) >> 6
    b_out = (b5 * 527 + 23) >> 6
    return (r_out, g_out, b_out)

PALETTE = {
    # Backgrounds: Deep Midnight Navy
    "bg_black": rgb565_quantize(6, 10, 16),           # Deep navy dark CRT glass (#060A10)
    "bg_header": rgb565_quantize(11, 19, 29),         # Dark muted slate blue header (#0B131D)
    "bg_footer": rgb565_quantize(11, 19, 29),         # Dark muted slate blue footer (#0B131D)
    "bg_card": rgb565_quantize(14, 25, 38),           # Secondary menu surface (#0E1926)
    
    # Graticule & Grid: Darkish Blues
    "grid_major": rgb565_quantize(21, 38, 58),        # Major division dotted lines (#15263A)
    "grid_minor": rgb565_quantize(32, 60, 92),        # Center crosshairs & graduations (#203C5C)
    "grid_border": rgb565_quantize(43, 78, 115),      # Graticule outer border (#2B4E73)
    
    # Phosphor Waveform Traces: Electric Cyan-Blue / Ice Phosphor
    "trace_blue": rgb565_quantize(0, 229, 255),       # Primary cyan-blue trace (#00E5FF)
    "trace_glow": rgb565_quantize(0, 72, 104),        # Trace bloom / sub-pixel glow (#004868)
    "trace_amber": rgb565_quantize(255, 176, 0),      # P4 CRT Amber for comparisons
    
    # Text & Readouts: High-Contrast Ice Cream & Muted Slate
    "text_primary": rgb565_quantize(226, 237, 248),   # High-contrast ice-white (#E2EDF8)
    "text_secondary": rgb565_quantize(130, 154, 177), # Muted slate blue-grey (#829AB1)
    "text_dark": rgb565_quantize(6, 10, 16),          # Inverse text
    
    # Indicators & States
    "run_blue": rgb565_quantize(0, 229, 255),         # RUN badge foreground (electric cyan)
    "run_bg": rgb565_quantize(0, 34, 61),             # RUN badge background (#00223D)
    "hold_orange": rgb565_quantize(255, 130, 0),      # HOLD badge foreground (warning amber)
    "hold_bg": rgb565_quantize(61, 27, 0),            # HOLD badge background (#3D1B00)
    "wait_amber": rgb565_quantize(255, 190, 0),       # WAIT badge foreground
    "trig_marker": rgb565_quantize(255, 184, 0),      # Trigger level line and flag (amber)
    "ground_marker": rgb565_quantize(0, 229, 255),    # Ground reference 0V flag (electric cyan)
    
    # Focus & Selection
    "focus_amber": rgb565_quantize(255, 196, 0),      # Active parameter focus (#FFC400)
    "focus_bg": rgb565_quantize(64, 48, 0),           # Focus highlight box
}

# -------------------------------------------------------------------------
# Drawing Engine
# -------------------------------------------------------------------------
class ScreenRenderer:
    def __init__(self, width=320, height=240):
        self.width = width
        self.height = height
        self.image = Image.new("RGB", (width, height), PALETTE["bg_black"])
        self.draw = ImageDraw.Draw(self.image)
        
    def fill_rect(self, x, y, w, h, color):
        self.draw.rectangle([x, y, x + w - 1, y + h - 1], fill=color)
        
    def draw_pixel(self, x, y, color):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.image.putpixel((x, y), color)
            
    def draw_char(self, x, y, char, fg, bg=None, scale=1):
        ascii_code = ord(char)
        if ascii_code >= 256 or (ascii_code * 5 + 4) >= len(GLCD_BYTES):
            return
        
        glyph_bytes = GLCD_BYTES[ascii_code * 5 : ascii_code * 5 + 5]
        cell_w = 6 * scale
        cell_h = 8 * scale
        
        if bg is not None:
            self.fill_rect(x, y, cell_w, cell_h, bg)
            
        for col in range(5):
            b = glyph_bytes[col]
            for row in range(7):
                if (b >> row) & 1:
                    if scale == 1:
                        self.draw_pixel(x + col, y + row, fg)
                    else:
                        self.fill_rect(x + col * scale, y + row * scale, scale, scale, fg)
                        
    def print_text(self, x, y, text, fg, bg=None, scale=1):
        cursor_x = x
        for c in text:
            self.draw_char(cursor_x, y, c, fg, bg, scale)
            cursor_x += 6 * scale

    def draw_badge(self, x, y, text, fg, bg, border=None):
        pad_x = 3
        pad_y = 1
        text_w = len(text) * 6
        total_w = text_w + pad_x * 2
        total_h = 8 + pad_y * 2
        
        self.fill_rect(x, y, total_w, total_h, bg)
        if border:
            self.draw.rectangle([x, y, x + total_w - 1, y + total_h - 1], outline=border)
        self.print_text(x + pad_x, y + pad_y, text, fg)
        return total_w

    def draw_graticule(self, x0=10, y0=20, w=300, h=192):
        """Draw classic laboratory oscilloscope reticle with tick marks in darkish blue."""
        # Outer rectangular border
        self.draw.rectangle([x0, y0, x0 + w, y0 + h], outline=PALETTE["grid_border"])
        
        div_w = 30
        div_h = 24
        
        cx = x0 + 5 * div_w  # 160
        cy = y0 + 4 * div_h  # 116
        
        # Internal dotted grid lines
        for i in range(1, 10):
            gx = x0 + i * div_w
            for gy in range(y0 + 1, y0 + h, 4):
                if gx != cx:
                    self.draw_pixel(gx, gy, PALETTE["grid_major"])
                    
        for j in range(1, 8):
            gy = y0 + j * div_h
            for gx in range(x0 + 1, x0 + w, 4):
                if gy != cy:
                    self.draw_pixel(gx, gy, PALETTE["grid_major"])
                    
        # Solid center crosshairs
        self.draw.line([x0 + 1, cy, x0 + w - 1, cy], fill=PALETTE["grid_minor"])
        self.draw.line([cx, y0 + 1, cx, y0 + h - 1], fill=PALETTE["grid_minor"])
        
        # Sub-division graduation tick marks on center axes
        for gx in range(x0, x0 + w + 1, 6):
            if (gx - x0) % 30 == 0:
                self.draw.line([gx, cy - 3, gx, cy + 3], fill=PALETTE["grid_border"])
            else:
                self.draw.line([gx, cy - 1, gx, cy + 1], fill=PALETTE["grid_minor"])
                
        for div in range(8):
            for sub in range(5):
                gy = int(y0 + div * 24 + sub * 4.8)
                if sub == 0:
                    self.draw.line([cx - 4, gy, cx + 4, gy], fill=PALETTE["grid_border"])
                else:
                    self.draw.line([cx - 2, gy, cx + 2, gy], fill=PALETTE["grid_minor"])
        self.draw.line([cx - 4, y0 + h, cx + 4, y0 + h], fill=PALETTE["grid_border"])

    def draw_ground_marker(self, x, y, channel=1):
        """Draw ground baseline marker 1▶ at left margin in electric cyan."""
        c = PALETTE["ground_marker"]
        self.draw_pixel(x, y - 2, c)
        self.draw_pixel(x + 1, y - 2, c)
        self.draw_pixel(x + 1, y - 1, c)
        self.draw_pixel(x + 1, y, c)
        self.draw_pixel(x + 1, y + 1, c)
        self.draw_pixel(x, y + 1, c)
        self.draw_pixel(x + 2, y + 1, c)
        self.draw.polygon([(x + 4, y - 3), (x + 8, y), (x + 4, y + 3)], fill=c)

    def draw_trigger_marker(self, x, y):
        """Draw trigger level flag ◀T at right margin with dashed line across screen."""
        c = PALETTE["trig_marker"]
        for gx in range(12, 308, 12):
            self.draw.line([gx, y, gx + 6, y], fill=(70, 52, 10))
            
        self.draw.polygon([(x + 5, y - 3), (x + 1, y), (x + 5, y + 3)], fill=c)
        self.draw.line([x + 6, y - 2, x + 8, y - 2], fill=c)
        self.draw.line([x + 7, y - 2, x + 7, y + 2], fill=c)

    def draw_trace_sine(self, x0=10, y0=20, w=300, h=192, freq=2.0, amp=50, offset=0):
        """Render luminous electric cyan-blue waveform trace."""
        cy = y0 + h // 2 + offset
        last_y = None
        for i in range(w):
            x = x0 + i
            angle = (i / w) * (2 * math.pi * freq)
            val = math.sin(angle)
            y = int(cy - val * amp)
            y = max(y0 + 1, min(y0 + h - 1, y))
            
            if last_y is not None:
                lo = min(y, last_y)
                hi = max(y, last_y)
                for py in range(lo - 1, hi + 2):
                    if y0 < py < y0 + h:
                        self.draw_pixel(x, py, PALETTE["trace_glow"])
                for py in range(lo, hi + 1):
                    self.draw_pixel(x, py, PALETTE["trace_blue"])
            else:
                self.draw_pixel(x, y, PALETTE["trace_blue"])
            last_y = y

    def draw_trace_square(self, x0=10, y0=20, w=300, h=192, periods=3, amp=48, offset=-48):
        """Render square wave with realistic ringing and overshoot."""
        cy = y0 + h // 2 + offset
        last_y = None
        for i in range(w):
            x = x0 + i
            phase = (i / (w / periods)) % 1.0
            
            if phase < 0.5:
                ring = math.exp(-phase * 16) * math.cos(phase * 40) * 12
                val = 1.0 + ring / amp
            else:
                p2 = phase - 0.5
                ring = math.exp(-p2 * 16) * math.cos(p2 * 40) * 12
                val = -1.0 - ring / amp
                
            y = int(cy - val * amp)
            y = max(y0 + 1, min(y0 + h - 1, y))
            
            if last_y is not None:
                lo = min(y, last_y)
                hi = max(y, last_y)
                for py in range(lo - 1, hi + 2):
                    if y0 < py < y0 + h:
                        self.draw_pixel(x, py, PALETTE["trace_glow"])
                for py in range(lo, hi + 1):
                    self.draw_pixel(x, py, PALETTE["trace_blue"])
            else:
                self.draw_pixel(x, y, PALETTE["trace_blue"])
            last_y = y

    def save_scaled(self, filepath, scale=4):
        self.image.save(filepath)
        scaled_path = filepath.replace(".png", "@4x.png")
        scaled_img = self.image.resize((self.width * scale, self.height * scale), Image.NEAREST)
        scaled_img.save(scaled_path)
        print(f"Generated: {filepath} ({self.width}x{self.height}) and {scaled_path}")

# -------------------------------------------------------------------------
# Screen 1: Main Scope Display - RUN Mode (Dark Blue Theme, Compact Bottom Bar)
# -------------------------------------------------------------------------
def render_screen_01_main_run(out_path):
    s = ScreenRenderer()
    
    # 1. Header Bar (Y: 0..17, Height 18)
    s.fill_rect(0, 0, 320, 18, PALETTE["bg_header"])
    s.draw.line([0, 18, 319, 18], fill=PALETTE["grid_major"])
    
    # RUN State Badge (Electric Cyan on Deep Navy)
    s.draw_badge(2, 3, "RUN", PALETTE["run_blue"], PALETTE["run_bg"], border=PALETTE["run_blue"])
    
    # Trigger Info
    s.print_text(36, 5, "TRIG:AUTO", PALETTE["text_primary"])
    s.print_text(96, 5, "[/]", PALETTE["focus_amber"])
    s.print_text(118, 5, "T:+1.25V", PALETTE["text_primary"])
    
    # Trigger Status Flag
    s.draw_badge(182, 3, "TRIG'D", PALETTE["run_blue"], PALETTE["bg_black"])
    
    s.print_text(244, 5, "LINK:OK", PALETTE["text_secondary"])
    s.draw_badge(290, 3, "M7", PALETTE["text_secondary"], PALETTE["bg_black"], border=PALETTE["grid_major"])
    
    # 2. Waveform Graticule & Trace (Y: 20..212, Height 192 px = 8 divs x 24 px)
    s.draw_graticule(10, 20, 300, 192)
    s.draw_ground_marker(1, 116, channel=1)   # 0V at center Y=116
    s.draw_trigger_marker(311, 92)           # Trigger level threshold at +1.25V
    s.draw_trace_sine(10, 20, 300, 192, freq=2.0, amp=50, offset=0)
    
    # 3. Compact Bottom Bar (Y: 216..239, Height 24 px - clean, compact 2-row layout)
    s.fill_rect(0, 216, 320, 24, PALETTE["bg_footer"])
    s.draw.line([0, 215, 319, 215], fill=PALETTE["grid_major"])
    
    # Row 1 (Y: 218): Primary Instrument Scales
    s.print_text(4, 218, "CH1: 500mV/div  DC  1X", PALETTE["text_primary"])
    s.print_text(168, 218, "TB: 1.00ms/div", PALETTE["text_primary"])
    s.print_text(260, 218, "f: 1.000kHz", PALETTE["focus_amber"])
    
    # Row 2 (Y: 228): Live Measurements Bar
    s.print_text(4, 228, "Vpp:2.50V", PALETTE["text_secondary"])
    s.print_text(74, 228, "Vrms:0.88V", PALETTE["text_secondary"])
    s.print_text(148, 228, "Vmax:+1.25V", PALETTE["text_secondary"])
    s.print_text(228, 228, "Vmin:-1.25V", PALETTE["text_secondary"])
    
    s.save_scaled(out_path)

# -------------------------------------------------------------------------
# Screen 2: Main Scope Display - HOLD Mode
# -------------------------------------------------------------------------
def render_screen_02_main_hold(out_path):
    s = ScreenRenderer()
    
    # 1. Header Bar
    s.fill_rect(0, 0, 320, 18, PALETTE["bg_header"])
    s.draw.line([0, 18, 319, 18], fill=PALETTE["grid_major"])
    
    # HOLD Badge
    s.draw_badge(2, 3, "HOLD", PALETTE["hold_orange"], PALETTE["hold_bg"], border=PALETTE["hold_orange"])
    
    s.print_text(38, 5, "TRIG:NORM", PALETTE["text_primary"])
    s.print_text(98, 5, "[\\]", PALETTE["focus_amber"])
    s.print_text(120, 5, "T:+1.65V", PALETTE["text_primary"])
    
    s.draw_badge(182, 3, "STOPPED", PALETTE["hold_orange"], PALETTE["bg_black"])
    s.print_text(244, 5, "BUF:1024", PALETTE["text_secondary"])
    s.draw_badge(296, 3, "1X", PALETTE["text_secondary"], PALETTE["bg_black"], border=PALETTE["grid_major"])
    
    # 2. Waveform & Graticule
    s.draw_graticule(10, 20, 300, 192)
    s.draw_ground_marker(1, 164, channel=1)   # Ground at -2 divs (0V baseline)
    s.draw_trigger_marker(311, 116)          # Trigger at +1.65V
    s.draw_trace_square(10, 20, 300, 192, periods=3, amp=48, offset=-48)
    
    # 3. Compact Bottom Bar
    s.fill_rect(0, 216, 320, 24, PALETTE["bg_footer"])
    s.draw.line([0, 215, 319, 215], fill=PALETTE["grid_major"])
    
    s.print_text(4, 218, "CH1: 1.00V/div  DC  1X", PALETTE["text_primary"])
    s.print_text(168, 218, "TB: 50.0us/div", PALETTE["text_primary"])
    s.print_text(260, 218, "f: 10.00kHz", PALETTE["focus_amber"])
    
    s.print_text(4, 228, "Vpp:3.32V", PALETTE["text_secondary"])
    s.print_text(74, 228, "Vrms:1.66V", PALETTE["text_secondary"])
    s.print_text(148, 228, "Vmax:+3.30V", PALETTE["text_secondary"])
    s.print_text(228, 228, "Vmin:+0.02V", PALETTE["text_secondary"])
    
    s.save_scaled(out_path)

# -------------------------------------------------------------------------
# Screen 3: Quick Focus Active
# -------------------------------------------------------------------------
def render_screen_03_quick_focus(out_path):
    s = ScreenRenderer()
    
    # 1. Header Bar
    s.fill_rect(0, 0, 320, 18, PALETTE["bg_header"])
    s.draw.line([0, 18, 319, 18], fill=PALETTE["grid_major"])
    s.draw_badge(2, 3, "RUN", PALETTE["run_blue"], PALETTE["run_bg"], border=PALETTE["run_blue"])
    s.print_text(36, 5, "TRIG:AUTO", PALETTE["text_primary"])
    s.print_text(96, 5, "[/]", PALETTE["focus_amber"])
    s.print_text(118, 5, "T:+1.25V", PALETTE["text_primary"])
    s.draw_badge(182, 3, "TRIG'D", PALETTE["run_blue"], PALETTE["bg_black"])
    s.print_text(244, 5, "FOCUS:ON", PALETTE["focus_amber"])
    
    # 2. Waveform & Graticule
    s.draw_graticule(10, 20, 300, 192)
    s.draw_ground_marker(1, 116, channel=1)
    s.draw_trigger_marker(311, 92)
    s.draw_trace_sine(10, 20, 300, 192, freq=2.0, amp=50, offset=0)
    
    # 3. Compact Bottom Bar: Quick-Focus Active on TIMEBASE
    s.fill_rect(0, 216, 320, 24, PALETTE["bg_footer"])
    s.draw.line([0, 215, 319, 215], fill=PALETTE["grid_major"])
    
    # Row 1: Scales with Timebase Selected (Highlighted with Amber Frame)
    s.print_text(4, 218, "CH1: 500mV/div  DC  1X", PALETTE["text_secondary"])
    
    tb_box_x = 164
    tb_box_w = 90
    s.fill_rect(tb_box_x, 217, tb_box_w, 10, PALETTE["focus_bg"])
    s.draw.rectangle([tb_box_x, 217, tb_box_x + tb_box_w - 1, 226], outline=PALETTE["focus_amber"])
    s.print_text(tb_box_x + 3, 218, "TB: 1.00ms/d", PALETTE["focus_amber"])
    s.print_text(260, 218, "f: 1.000kHz", PALETTE["text_secondary"])
    
    # Row 2: Control Action Prompt
    s.print_text(4, 229, "> ADJ TB: 1.00ms", PALETTE["focus_amber"])
    s.print_text(130, 229, "[SEL:Next  +/-:Step  OK:Run]", PALETTE["text_primary"])
    
    s.save_scaled(out_path)

# -------------------------------------------------------------------------
# Screen 4: Settings & Calibration Menu
# -------------------------------------------------------------------------
def render_screen_04_menu_settings(out_path):
    s = ScreenRenderer()
    
    # Dimmed background graticule & trace
    s.draw_graticule(10, 20, 300, 192)
    s.draw_trace_sine(10, 20, 300, 192, freq=2.0, amp=50, offset=0)
    
    # Dark blue technical console overlay
    overlay_x = 18
    overlay_y = 12
    overlay_w = 284
    overlay_h = 216
    
    s.fill_rect(overlay_x, overlay_y, overlay_w, overlay_h, PALETTE["bg_card"])
    s.draw.rectangle([overlay_x, overlay_y, overlay_x + overlay_w - 1, overlay_y + overlay_h - 1], outline=PALETTE["grid_border"], width=1)
    s.draw.rectangle([overlay_x + 2, overlay_y + 2, overlay_x + overlay_w - 3, overlay_y + overlay_h - 3], outline=PALETTE["grid_major"])
    
    # Header
    s.fill_rect(overlay_x + 3, overlay_y + 3, overlay_w - 6, 16, PALETTE["bg_header"])
    s.print_text(overlay_x + 12, overlay_y + 7, "=== INSTRUMENT CONFIGURATION ===", PALETTE["text_primary"])
    
    items = [
        ("01. TIMEBASE       :", "1.00 ms/div", False),
        ("02. TRIGGER MODE   :", "AUTO", False),
        ("03. TRIGGER LEVEL  :", "+1.25 V  (1550)", False),
        ("04. TRIGGER SLOPE  :", "RISING  [/]", False),
        ("05. ZERO CALIBRATE :", "COMBO #4 [1195]", True),   # Selected!
        ("06. OFFSET CAL     :", "+0000 uV", False),
        ("07. GAIN CAL       :", "65536 (1.000)", False),
        ("08. SYSTEM DIAG    :", "ENTER >", False),
    ]
    
    start_y = overlay_y + 28
    for idx, (label, val, is_active) in enumerate(items):
        item_y = start_y + idx * 17
        if is_active:
            s.fill_rect(overlay_x + 6, item_y - 2, overlay_w - 12, 14, PALETTE["focus_bg"])
            s.draw.rectangle([overlay_x + 6, item_y - 2, overlay_x + overlay_w - 7, item_y + 11], outline=PALETTE["focus_amber"])
            s.print_text(overlay_x + 12, item_y + 1, "> " + label, PALETTE["focus_amber"])
            s.print_text(overlay_x + 150, item_y + 1, val, PALETTE["text_primary"])
        else:
            s.print_text(overlay_x + 16, item_y + 1, label, PALETTE["text_secondary"])
            s.print_text(overlay_x + 150, item_y + 1, val, PALETTE["text_primary"])
            
    # Bottom Navigation Prompt Bar
    prompt_y = overlay_y + overlay_h - 22
    s.fill_rect(overlay_x + 3, prompt_y, overlay_w - 6, 19, PALETTE["bg_black"])
    s.draw.line([overlay_x + 3, prompt_y, overlay_x + overlay_w - 4, prompt_y], fill=PALETTE["grid_major"])
    s.print_text(overlay_x + 8, prompt_y + 6, "[SEL:Next  +/-:Edit  OK:Cap  HOLD SEL:Exit]", PALETTE["focus_amber"])
    
    s.save_scaled(out_path)

# -------------------------------------------------------------------------
# Screen 5: System Diagnostics & Hardware Calibration Matrix
# -------------------------------------------------------------------------
def render_screen_05_diagnostics(out_path):
    s = ScreenRenderer()
    
    s.fill_rect(0, 0, 320, 240, PALETTE["bg_black"])
    
    # Title Plate
    s.fill_rect(0, 0, 320, 16, PALETTE["bg_header"])
    s.draw.line([0, 16, 319, 16], fill=PALETTE["grid_border"])
    s.print_text(8, 4, "=== DSO183 SYSTEM & CALIBRATION MATRIX ===", PALETTE["text_primary"])
    
    # Column 1: System & Firmware
    col1_x = 8
    s.print_text(col1_x, 22, "-- SYSTEM TELEMETRY --", PALETTE["focus_amber"])
    s.print_text(col1_x, 34, "BUILD : 0x4453424D #051A", PALETTE["text_primary"])
    s.print_text(col1_x, 44, "BOOTS : 0014  LOOP: 184Hz", PALETTE["text_secondary"])
    s.print_text(col1_x, 54, "TICK  : 00:42:15.820", PALETTE["text_secondary"])
    s.print_text(col1_x, 64, "PANEL : ILI9341 ID:9341", PALETTE["text_secondary"])
    s.print_text(col1_x, 74, "MADCTL: 0x20 (LANDSCAPE)", PALETTE["text_secondary"])
    s.print_text(col1_x, 84, "ERRORS: 0x00000000 [OK]", PALETTE["run_blue"])
    
    # Column 2: Front-Panel Switches & Hardware
    col2_x = 164
    s.print_text(col2_x, 22, "-- SWITCHES & ADC1 --", PALETTE["focus_amber"])
    s.print_text(col2_x, 34, "SEN1 (GAIN) : x2  (1840)", PALETTE["text_primary"])
    s.print_text(col2_x, 44, "SEN2 (RANGE): 1V  (3120)", PALETTE["text_primary"])
    s.print_text(col2_x, 54, "CPL  (COUP) : DC  (2048)", PALETTE["text_secondary"])
    s.print_text(col2_x, 64, "DECODED COMBO: #4 (500mV)", PALETTE["run_blue"])
    s.print_text(col2_x, 74, "BTNS: OK:0 +:0 -:0 SEL:0", PALETTE["text_secondary"])
    s.print_text(col2_x, 84, "RAW GPIOA IDR: 0x00FF", PALETTE["text_secondary"])
    
    s.draw.line([8, 98, 311, 98], fill=PALETTE["grid_major"])
    
    # Calibration Matrix (9 Combos)
    s.print_text(8, 104, "-- ZERO CALIBRATION MATRIX (ADC COUNTS) --", PALETTE["focus_amber"])
    s.print_text(8, 116, "RANGE   GAIN x1       GAIN x2       GAIN x5", PALETTE["text_secondary"])
    s.draw.line([8, 126, 311, 126], fill=PALETTE["grid_major"])
    
    matrix = [
        ("1.0V ", "C0: 1192 [OK]", "C1: 1195 [OK]", "C2: 1190 [OK]"),
        ("0.1V ", "C3: 1194 [OK]", "C4: 1196 [OK]", "C5: 1193 [OK]"),
        ("10mV ", "C6: 1198 [OK]", "C7: 1201 [OK]", "C8: 1197 [OK]"),
    ]
    
    for idx, (rng, g1, g2, g5) in enumerate(matrix):
        row_y = 130 + idx * 12
        s.print_text(8, row_y, rng, PALETTE["text_primary"])
        s.print_text(52, row_y, g1, PALETTE["run_blue"] if idx != 1 else PALETTE["text_primary"])
        s.print_text(138, row_y, g2, PALETTE["focus_amber"] if idx == 1 else PALETTE["text_primary"])
        s.print_text(224, row_y, g5, PALETTE["run_blue"])
        
    s.draw.line([8, 170, 311, 170], fill=PALETTE["grid_major"])
    
    # Resource Usage
    s.print_text(8, 176, "-- RESOURCE UTILISATION --", PALETTE["focus_amber"])
    s.print_text(8, 188, "FLASH: 23,304 / 65,536 BYTES (35.6% USED - 42.2 KB FREE)", PALETTE["text_secondary"])
    s.print_text(8, 198, "SRAM :  9,620 / 20,480 BYTES (46.9% USED - 10.8 KB FREE)", PALETTE["text_secondary"])
    s.print_text(8, 208, "FRAME REFRESH: DIRECT GRAM STREAM (ZERO HEAP ALLOCATION)", PALETTE["text_secondary"])
    
    # Footer
    s.fill_rect(0, 222, 320, 18, PALETTE["bg_header"])
    s.draw.line([0, 222, 319, 222], fill=PALETTE["grid_border"])
    s.print_text(8, 226, "[PRESS OK OR SEL TO RETURN TO MAIN SCOPE]", PALETTE["text_primary"])
    
    s.save_scaled(out_path)

# -------------------------------------------------------------------------
# SVG Vector Mockups Generator (Dark Blue Theme)
# -------------------------------------------------------------------------
def generate_svg_mockups(svg_dir):
    """Generate high-quality SVG vector mockups with darkish blue palette."""
    # SVG 1: Main Scope Run
    svg_run = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 240" width="640" height="480" style="background:#060a10;font-family:monospace;font-size:7px;letter-spacing:0.5px;">
  <defs>
    <filter id="glow" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur stdDeviation="0.7" result="blur" />
      <feMerge>
        <feMergeNode in="blur" />
        <feMergeNode in="SourceGraphic" />
      </feMerge>
    </filter>
  </defs>

  <!-- Header Bar -->
  <rect x="0" y="0" width="320" height="18" fill="#0b131d" stroke="#15263a" stroke-width="0.5" />
  <!-- RUN Badge -->
  <rect x="2" y="3" width="26" height="12" fill="#00223d" stroke="#00e5ff" stroke-width="0.7" rx="1" />
  <text x="5" y="12" fill="#00e5ff" font-weight="bold">RUN</text>
  
  <!-- Trigger Info -->
  <text x="36" y="12" fill="#e2edf8">TRIG:AUTO</text>
  <text x="96" y="12" fill="#ffc400">[/]</text>
  <text x="118" y="12" fill="#e2edf8">T:+1.25V</text>
  
  <!-- Trig'd Badge -->
  <rect x="180" y="3" width="38" height="12" fill="#060a10" stroke="#15263a" stroke-width="0.5" />
  <text x="183" y="12" fill="#00e5ff" font-weight="bold">TRIG'D</text>
  
  <text x="244" y="12" fill="#829ab1">LINK:OK</text>
  <rect x="290" y="3" width="22" height="12" fill="#060a10" stroke="#15263a" stroke-width="0.5" />
  <text x="294" y="12" fill="#829ab1">M7</text>

  <!-- Graticule 300x192 (Darkish Blue) -->
  <rect x="10" y="20" width="300" height="192" fill="#04070c" stroke="#2b4e73" stroke-width="1" />
  
  <!-- Dotted Grid Lines (10 x 8 divs) -->
  <g stroke="#15263a" stroke-width="0.5" stroke-dasharray="1,3">
    <line x1="40" y1="20" x2="40" y2="212" /><line x1="70" y1="20" x2="70" y2="212" /><line x1="100" y1="20" x2="100" y2="212" />
    <line x1="130" y1="20" x2="130" y2="212" /><line x1="190" y1="20" x2="190" y2="212" /><line x1="220" y1="20" x2="220" y2="212" />
    <line x1="250" y1="20" x2="250" y2="212" /><line x1="280" y1="20" x2="280" y2="212" />
    <line x1="10" y1="44" x2="310" y2="44" /><line x1="10" y1="68" x2="310" y2="68" /><line x1="10" y1="92" x2="310" y2="92" />
    <line x1="10" y1="140" x2="310" y2="140" /><line x1="10" y1="164" x2="310" y2="164" /><line x1="10" y1="188" x2="310" y2="188" />
  </g>

  <!-- Solid Center Axes with Reticle Ticks -->
  <line x1="10" y1="116" x2="310" y2="116" stroke="#203c5c" stroke-width="0.8" />
  <line x1="160" y1="20" x2="160" y2="212" stroke="#203c5c" stroke-width="0.8" />
  
  <!-- Trigger Level Line (Dashed) -->
  <line x1="10" y1="92" x2="310" y2="92" stroke="#ffb800" stroke-width="0.6" stroke-dasharray="4,4" opacity="0.6" />
  <polygon points="312,89 317,92 312,95" fill="#ffb800" />
  <text x="313" y="102" fill="#ffb800" font-size="6px">T</text>

  <!-- Ground Reference Marker -->
  <polygon points="8,116 3,113 3,119" fill="#00e5ff" />
  <text x="0" y="118" fill="#00e5ff" font-size="6px" font-weight="bold">1</text>

  <!-- Waveform (Electric Cyan-Blue Sine wave) -->
  <path d="M 10 116 Q 47.5 50 85 116 T 160 116 T 235 116 T 310 116" fill="none" stroke="#00e5ff" stroke-width="1.3" filter="url(#glow)" />

  <!-- Compact Bottom Instrument Bar (Height: 24 px) -->
  <rect x="0" y="216" width="320" height="24" fill="#0b131d" stroke="#15263a" stroke-width="0.5" />
  
  <!-- Row 1: Primary Scales -->
  <text x="4" y="225" fill="#e2edf8" font-weight="bold">CH1: 500mV/div  DC  1X</text>
  <text x="168" y="225" fill="#e2edf8">TB: 1.00ms/div</text>
  <text x="260" y="225" fill="#ffc400" font-weight="bold">f: 1.000kHz</text>
  
  <!-- Row 2: Live Measurements -->
  <text x="4" y="235" fill="#829ab1">Vpp:2.50V</text>
  <text x="74" y="235" fill="#829ab1">Vrms:0.88V</text>
  <text x="148" y="235" fill="#829ab1">Vmax:+1.25V</text>
  <text x="228" y="235" fill="#829ab1">Vmin:-1.25V</text>
</svg>"""

    # SVG 2: Main Scope Hold
    svg_hold = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 240" width="640" height="480" style="background:#060a10;font-family:monospace;font-size:7px;letter-spacing:0.5px;">
  <!-- Header Bar -->
  <rect x="0" y="0" width="320" height="18" fill="#0b131d" stroke="#15263a" stroke-width="0.5" />
  <rect x="2" y="3" width="28" height="12" fill="#3d1b00" stroke="#ff8200" stroke-width="0.7" rx="1" />
  <text x="4" y="12" fill="#ff8200" font-weight="bold">HOLD</text>
  
  <text x="38" y="12" fill="#e2edf8">TRIG:NORM</text>
  <text x="98" y="12" fill="#ffc400">[\\]</text>
  <text x="120" y="12" fill="#e2edf8">T:+1.65V</text>
  
  <rect x="180" y="3" width="46" height="12" fill="#060a10" stroke="#15263a" stroke-width="0.5" />
  <text x="183" y="12" fill="#ff8200" font-weight="bold">STOPPED</text>
  <text x="244" y="12" fill="#829ab1">BUF:1024</text>
  <rect x="296" y="3" width="18" height="12" fill="#060a10" stroke="#15263a" stroke-width="0.5" />
  <text x="300" y="12" fill="#829ab1">1X</text>

  <!-- Graticule 300x192 -->
  <rect x="10" y="20" width="300" height="192" fill="#04070c" stroke="#2b4e73" stroke-width="1" />
  <g stroke="#15263a" stroke-width="0.5" stroke-dasharray="1,3">
    <line x1="40" y1="20" x2="40" y2="212" /><line x1="70" y1="20" x2="70" y2="212" /><line x1="100" y1="20" x2="100" y2="212" />
    <line x1="130" y1="20" x2="130" y2="212" /><line x1="190" y1="20" x2="190" y2="212" /><line x1="220" y1="20" x2="220" y2="212" />
    <line x1="250" y1="20" x2="250" y2="212" /><line x1="280" y1="20" x2="280" y2="212" />
    <line x1="10" y1="44" x2="310" y2="44" /><line x1="10" y1="68" x2="310" y2="68" /><line x1="10" y1="92" x2="310" y2="92" />
    <line x1="10" y1="140" x2="310" y2="140" /><line x1="10" y1="164" x2="310" y2="164" /><line x1="10" y1="188" x2="310" y2="188" />
  </g>
  <line x1="10" y1="116" x2="310" y2="116" stroke="#203c5c" stroke-width="0.8" />
  <line x1="160" y1="20" x2="160" y2="212" stroke="#203c5c" stroke-width="0.8" />

  <!-- Ground Reference Marker -->
  <polygon points="8,164 3,161 3,167" fill="#00e5ff" />
  <text x="0" y="166" fill="#00e5ff" font-size="6px" font-weight="bold">1</text>
  
  <!-- Trigger Marker -->
  <polygon points="312,113 317,116 312,119" fill="#ffb800" />
  <text x="313" y="126" fill="#ffb800" font-size="6px">T</text>
  <line x1="10" y1="116" x2="310" y2="116" stroke="#ffb800" stroke-width="0.6" stroke-dasharray="4,4" opacity="0.6" />

  <!-- Frozen Waveform -->
  <path d="M 10 164 L 10 68 L 60 68 L 60 164 L 110 164 L 110 68 L 160 68 L 160 164 L 210 164 L 210 68 L 260 68 L 260 164 L 310 164" fill="none" stroke="#00e5ff" stroke-width="1.2" />

  <!-- Compact Bottom Bar -->
  <rect x="0" y="216" width="320" height="24" fill="#0b131d" stroke="#15263a" stroke-width="0.5" />
  <text x="4" y="225" fill="#e2edf8" font-weight="bold">CH1: 1.00V/div  DC  1X</text>
  <text x="168" y="225" fill="#e2edf8">TB: 50.0us/div</text>
  <text x="260" y="225" fill="#ffc400" font-weight="bold">f: 10.00kHz</text>
  <text x="4" y="235" fill="#829ab1">Vpp:3.32V</text>
  <text x="74" y="235" fill="#829ab1">Vrms:1.66V</text>
  <text x="148" y="235" fill="#829ab1">Vmax:+3.30V</text>
  <text x="228" y="235" fill="#829ab1">Vmin:+0.02V</text>
</svg>"""

    # SVG 3: Quick Focus Active
    svg_focus = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 240" width="640" height="480" style="background:#060a10;font-family:monospace;font-size:7px;letter-spacing:0.5px;">
  <!-- Header Bar -->
  <rect x="0" y="0" width="320" height="18" fill="#0b131d" stroke="#15263a" stroke-width="0.5" />
  <rect x="2" y="3" width="26" height="12" fill="#00223d" stroke="#00e5ff" stroke-width="0.7" rx="1" />
  <text x="5" y="12" fill="#00e5ff" font-weight="bold">RUN</text>
  <text x="36" y="12" fill="#e2edf8">TRIG:AUTO</text>
  <text x="96" y="12" fill="#ffc400">[/]</text>
  <text x="118" y="12" fill="#e2edf8">T:+1.25V</text>
  <text x="183" y="12" fill="#00e5ff" font-weight="bold">TRIG'D</text>
  <text x="244" y="12" fill="#ffc400">FOCUS:ON</text>

  <!-- Graticule 300x192 -->
  <rect x="10" y="20" width="300" height="192" fill="#04070c" stroke="#2b4e73" stroke-width="1" />
  <path d="M 10 116 Q 47.5 50 85 116 T 160 116 T 235 116 T 310 116" fill="none" stroke="#00e5ff" stroke-width="1.2" opacity="0.9" />

  <!-- Compact Bottom Bar -->
  <rect x="0" y="216" width="320" height="24" fill="#0b131d" stroke="#15263a" stroke-width="0.5" />
  <text x="4" y="225" fill="#829ab1">CH1: 500mV/div  DC  1X</text>
  
  <!-- Active Focus Highlight on Timebase -->
  <rect x="164" y="217" width="90" height="9" fill="#403000" stroke="#ffc400" stroke-width="0.8" />
  <text x="167" y="224" fill="#ffc400" font-weight="bold">TB: 1.00ms/d</text>
  <text x="260" y="225" fill="#829ab1">f: 1.000kHz</text>
  
  <text x="4" y="235" fill="#ffc400" font-weight="bold">&gt; ADJ TB: 1.00ms</text>
  <text x="130" y="235" fill="#e2edf8">[SEL:Next  +/-:Step  OK:Run]</text>
</svg>"""

    # SVG 4: Settings Menu
    svg_settings = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 240" width="640" height="480" style="background:#060a10;font-family:monospace;font-size:7px;letter-spacing:0.5px;">
  <!-- Background Graticule (Dimmed) -->
  <rect x="10" y="20" width="300" height="192" fill="#04070c" stroke="#15263a" stroke-width="0.5" opacity="0.4" />
  
  <!-- Settings Dialog Overlay -->
  <rect x="18" y="12" width="284" height="216" fill="#0e1926" stroke="#2b4e73" stroke-width="1.2" rx="2" />
  <rect x="20" y="14" width="280" height="212" fill="none" stroke="#15263a" stroke-width="0.5" />
  
  <!-- Title Plate -->
  <rect x="21" y="15" width="278" height="16" fill="#0b131d" />
  <text x="36" y="26" fill="#e2edf8" font-weight="bold">=== INSTRUMENT CONFIGURATION ===</text>

  <!-- Items -->
  <text x="34" y="46" fill="#829ab1">01. TIMEBASE       :</text><text x="168" y="46" fill="#e2edf8">1.00 ms/div</text>
  <text x="34" y="63" fill="#829ab1">02. TRIGGER MODE   :</text><text x="168" y="63" fill="#e2edf8">AUTO</text>
  <text x="34" y="80" fill="#829ab1">03. TRIGGER LEVEL  :</text><text x="168" y="80" fill="#e2edf8">+1.25 V  (1550)</text>
  <text x="34" y="97" fill="#829ab1">04. TRIGGER SLOPE  :</text><text x="168" y="97" fill="#e2edf8">RISING  [/]</text>
  
  <!-- Highlighted Item (Cal Zero) -->
  <rect x="24" y="108" width="272" height="15" fill="#403000" stroke="#ffc400" stroke-width="0.8" />
  <text x="30" y="119" fill="#ffc400" font-weight="bold">&gt; 05. ZERO CALIBRATE :</text><text x="168" y="119" fill="#ffffff" font-weight="bold">COMBO #4 [1195]</text>

  <text x="34" y="138" fill="#829ab1">06. OFFSET CAL     :</text><text x="168" y="138" fill="#e2edf8">+0000 uV</text>
  <text x="34" y="155" fill="#829ab1">07. GAIN CAL       :</text><text x="168" y="155" fill="#e2edf8">65536 (1.000)</text>
  <text x="34" y="172" fill="#829ab1">08. SYSTEM DIAG    :</text><text x="168" y="172" fill="#e2edf8">ENTER &gt;</text>

  <!-- Prompt Footer -->
  <rect x="21" y="206" width="278" height="19" fill="#060a10" stroke="#15263a" stroke-width="0.5" />
  <text x="26" y="219" fill="#ffc400">[SEL:Next  +/-:Edit  OK:Cap  HOLD SEL:Exit]</text>
</svg>"""

    # SVG 5: Diagnostics Matrix
    svg_diag = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 240" width="640" height="480" style="background:#060a10;font-family:monospace;font-size:7px;letter-spacing:0.5px;">
  <rect x="0" y="0" width="320" height="16" fill="#0b131d" stroke="#2b4e73" stroke-width="0.5" />
  <text x="8" y="11" fill="#e2edf8" font-weight="bold">=== DSO183 SYSTEM &amp; CALIBRATION MATRIX ===</text>

  <text x="8" y="28" fill="#ffc400">-- SYSTEM TELEMETRY --</text>
  <text x="8" y="38" fill="#e2edf8">BUILD : 0x4453424D #051A</text>
  <text x="8" y="48" fill="#829ab1">BOOTS : 0014  LOOP: 184Hz</text>
  <text x="8" y="58" fill="#829ab1">TICK  : 00:42:15.820</text>
  <text x="8" y="68" fill="#829ab1">PANEL : ILI9341 ID:9341</text>
  <text x="8" y="78" fill="#829ab1">MADCTL: 0x20 (LANDSCAPE)</text>
  <text x="8" y="88" fill="#00e5ff">ERRORS: 0x00000000 [OK]</text>

  <text x="164" y="28" fill="#ffc400">-- SWITCHES &amp; ADC1 --</text>
  <text x="164" y="38" fill="#e2edf8">SEN1 (GAIN) : x2  (1840)</text>
  <text x="164" y="48" fill="#e2edf8">SEN2 (RANGE): 1V  (3120)</text>
  <text x="164" y="58" fill="#829ab1">CPL  (COUP) : DC  (2048)</text>
  <text x="164" y="68" fill="#00e5ff">DECODED COMBO: #4 (500mV)</text>
  <text x="164" y="78" fill="#829ab1">BTNS: OK:0 +:0 -:0 SEL:0</text>
  <text x="164" y="88" fill="#829ab1">RAW GPIOA IDR: 0x00FF</text>

  <line x1="8" y1="96" x2="311" y2="96" stroke="#15263a" stroke-width="0.5" />
  <text x="8" y="106" fill="#ffc400">-- ZERO CALIBRATION MATRIX (ADC COUNTS) --</text>
  <text x="8" y="116" fill="#829ab1">RANGE   GAIN x1       GAIN x2       GAIN x5</text>
  <line x1="8" y1="120" x2="311" y2="120" stroke="#15263a" stroke-width="0.5" />
  
  <text x="8" y="130" fill="#e2edf8">1.0V </text><text x="52" y="130" fill="#00e5ff">C0: 1192 [OK]</text><text x="138" y="130" fill="#00e5ff">C1: 1195 [OK]</text><text x="224" y="130" fill="#00e5ff">C2: 1190 [OK]</text>
  <text x="8" y="142" fill="#e2edf8">0.1V </text><text x="52" y="142" fill="#00e5ff">C3: 1194 [OK]</text><text x="138" y="142" fill="#ffc400">C4: 1196 [OK]</text><text x="224" y="142" fill="#00e5ff">C5: 1193 [OK]</text>
  <text x="8" y="154" fill="#e2edf8">10mV </text><text x="52" y="154" fill="#00e5ff">C6: 1198 [OK]</text><text x="138" y="154" fill="#00e5ff">C7: 1201 [OK]</text><text x="224" y="154" fill="#00e5ff">C8: 1197 [OK]</text>

  <line x1="8" y1="164" x2="311" y2="164" stroke="#15263a" stroke-width="0.5" />
  <text x="8" y="174" fill="#ffc400">-- RESOURCE UTILISATION --</text>
  <text x="8" y="184" fill="#829ab1">FLASH: 23,304 / 65,536 BYTES (35.6% USED - 42.2 KB FREE)</text>
  <text x="8" y="194" fill="#829ab1">SRAM :  9,620 / 20,480 BYTES (46.9% USED - 10.8 KB FREE)</text>
  <text x="8" y="204" fill="#829ab1">FRAME REFRESH: DIRECT GRAM STREAM (ZERO HEAP ALLOCATION)</text>

  <rect x="0" y="222" width="320" height="18" fill="#0b131d" stroke="#2b4e73" stroke-width="0.5" />
  <text x="8" y="233" fill="#e2edf8">[PRESS OK OR SEL TO RETURN TO MAIN SCOPE]</text>
</svg>"""

    files = [
        ("mockup_screen_01_main_run.svg", svg_run),
        ("mockup_screen_02_main_hold.svg", svg_hold),
        ("mockup_screen_03_quick_focus.svg", svg_focus),
        ("mockup_screen_04_menu_settings.svg", svg_settings),
        ("mockup_screen_05_diagnostics.svg", svg_diag),
    ]
    
    for filename, content in files:
        path = os.path.join(svg_dir, filename)
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"Generated SVG: {filename}")

if __name__ == "__main__":
    mockups_dir = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(mockups_dir, exist_ok=True)
    
    render_screen_01_main_run(os.path.join(mockups_dir, "screen_01_main_run.png"))
    render_screen_02_main_hold(os.path.join(mockups_dir, "screen_02_main_hold.png"))
    render_screen_03_quick_focus(os.path.join(mockups_dir, "screen_03_quick_focus.png"))
    render_screen_04_menu_settings(os.path.join(mockups_dir, "screen_04_menu_settings.png"))
    render_screen_05_diagnostics(os.path.join(mockups_dir, "screen_05_diagnostics.png"))
    
    generate_svg_mockups(mockups_dir)
    print("All mockups regenerated successfully with dark blue theme!")
