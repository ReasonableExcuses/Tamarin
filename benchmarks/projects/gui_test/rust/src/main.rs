// ============================================================================
// Rust GUI Calculator - High-Performance Native Raylib 5.5 App
// ============================================================================
use std::env;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_uchar, c_uint};

#[repr(C)]
#[derive(Copy, Clone)]
pub struct Vector2 {
    pub x: f32,
    pub y: f32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct Color {
    pub r: c_uchar,
    pub g: c_uchar,
    pub b: c_uchar,
    pub a: c_uchar,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct Rectangle {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}

pub const FLAG_WINDOW_HIDDEN: c_uint = 128;
pub const MOUSE_BUTTON_LEFT: c_int = 0;

pub const KEY_MINUS: c_int = 45;
pub const KEY_PERIOD: c_int = 46;
pub const KEY_SLASH: c_int = 47;
pub const KEY_ZERO: c_int = 48;
pub const KEY_ONE: c_int = 49;
pub const KEY_TWO: c_int = 50;
pub const KEY_THREE: c_int = 51;
pub const KEY_FOUR: c_int = 52;
pub const KEY_FIVE: c_int = 53;
pub const KEY_SIX: c_int = 54;
pub const KEY_SEVEN: c_int = 55;
pub const KEY_EIGHT: c_int = 56;
pub const KEY_NINE: c_int = 57;
pub const KEY_EQUAL: c_int = 61;
pub const KEY_ENTER: c_int = 257;
pub const KEY_BACKSPACE: c_int = 259;
pub const KEY_ESCAPE: c_int = 256;
pub const KEY_KP_0: c_int = 320;
pub const KEY_KP_1: c_int = 321;
pub const KEY_KP_2: c_int = 322;
pub const KEY_KP_3: c_int = 323;
pub const KEY_KP_4: c_int = 324;
pub const KEY_KP_5: c_int = 325;
pub const KEY_KP_6: c_int = 326;
pub const KEY_KP_7: c_int = 327;
pub const KEY_KP_8: c_int = 328;
pub const KEY_KP_9: c_int = 329;
pub const KEY_KP_DECIMAL: c_int = 330;
pub const KEY_KP_DIVIDE: c_int = 331;
pub const KEY_KP_MULTIPLY: c_int = 332;
pub const KEY_KP_SUBTRACT: c_int = 333;
pub const KEY_KP_ADD: c_int = 334;
pub const KEY_KP_ENTER: c_int = 335;
pub const KEY_C: c_int = 67;

extern "C" {
    fn InitWindow(width: c_int, height: c_int, title: *const c_char);
    fn CloseWindow();
    fn WindowShouldClose() -> bool;
    fn SetConfigFlags(flags: c_uint);
    fn SetTargetFPS(fps: c_int);
    fn BeginDrawing();
    fn EndDrawing();
    fn ClearBackground(color: Color);
    fn DrawRectangleRec(rec: Rectangle, color: Color);
    fn DrawRectangleLinesEx(rec: Rectangle, lineThick: f32, color: Color);
    fn DrawText(text: *const c_char, posX: c_int, posY: c_int, fontSize: c_int, color: Color);
    fn MeasureText(text: *const c_char, fontSize: c_int) -> c_int;
    fn GetMousePosition() -> Vector2;
    fn IsMouseButtonPressed(button: c_int) -> bool;
    fn IsKeyPressed(key: c_int) -> bool;
    fn CheckCollisionPointRec(point: Vector2, rec: Rectangle) -> bool;
}

// ── Calculator State ────────────────────────────────────────────────────────
struct CalcState {
    display: String,
    expr: String,
    stored_val: f64,
    pending_op: Option<char>,
    has_pending_op: bool,
    should_reset_on_digit: bool,
    has_error: bool,
}

impl CalcState {
    fn new() -> Self {
        Self {
            display: String::from("0"),
            expr: String::new(),
            stored_val: 0.0,
            pending_op: None,
            has_pending_op: false,
            should_reset_on_digit: false,
            has_error: false,
        }
    }

    fn reset(&mut self) {
        self.display = String::from("0");
        self.expr.clear();
        self.stored_val = 0.0;
        self.pending_op = None;
        self.has_pending_op = false;
        self.should_reset_on_digit = false;
        self.has_error = false;
    }

    fn append_digit(&mut self, digit: char) {
        if self.has_error || self.should_reset_on_digit || self.display == "0" {
            self.display.clear();
            self.display.push(digit);
            self.should_reset_on_digit = false;
            self.has_error = false;
        } else if self.display.len() < 15 {
            self.display.push(digit);
        }
    }

    fn append_dot(&mut self) {
        if self.has_error || self.should_reset_on_digit {
            self.display = String::from("0.");
            self.should_reset_on_digit = false;
            self.has_error = false;
            return;
        }
        if !self.display.contains('.') && self.display.len() < 15 {
            self.display.push('.');
        }
    }

    fn backspace(&mut self) {
        if self.has_error || self.should_reset_on_digit {
            self.reset();
            return;
        }
        self.display.pop();
        if self.display.is_empty() {
            self.display.push('0');
        }
    }

    fn toggle_sign(&mut self) {
        if self.has_error || self.display == "0" {
            return;
        }
        if self.display.starts_with('-') {
            self.display.remove(0);
        } else {
            self.display.insert(0, '-');
        }
    }

    fn percent(&mut self) {
        if self.has_error {
            return;
        }
        if let Ok(v) = self.display.parse::<f64>() {
            let res = v / 100.0;
            self.display = format_number(res);
        }
    }

    fn execute_pending(&mut self) {
        if !self.has_pending_op {
            return;
        }
        let current = self.display.parse::<f64>().unwrap_or(0.0);
        let mut result = 0.0;
        if let Some(op) = self.pending_op {
            match op {
                '+' => result = self.stored_val + current,
                '-' => result = self.stored_val - current,
                '*' => result = self.stored_val * current,
                '/' => {
                    if current == 0.0 {
                        self.has_error = true;
                        self.display = String::from("Error");
                        self.has_pending_op = false;
                        self.should_reset_on_digit = true;
                        return;
                    }
                    result = self.stored_val / current;
                }
                _ => {}
            }
        }
        self.display = format_number(result);
        self.stored_val = result;
    }

    fn set_operator(&mut self, op: char) {
        if self.has_error {
            return;
        }
        if self.has_pending_op && !self.should_reset_on_digit {
            self.execute_pending();
        } else {
            self.stored_val = self.display.parse::<f64>().unwrap_or(0.0);
        }
        self.pending_op = Some(op);
        self.has_pending_op = true;
        self.should_reset_on_digit = true;
        self.expr = format!("{} {}", self.display, op);
    }

    fn equals(&mut self) {
        if self.has_error || !self.has_pending_op {
            return;
        }
        self.execute_pending();
        self.has_pending_op = false;
        self.pending_op = None;
        self.should_reset_on_digit = true;
        self.expr.clear();
    }
}

fn format_number(val: f64) -> String {
    if val.is_nan() {
        return String::from("Err");
    }
    let int_part = val as i64;
    let frac_part = (val - int_part as f64).abs();
    if frac_part < 0.0000001 {
        format!("{}", int_part)
    } else {
        let s = format!("{:.6}", val);
        let trimmed = s.trim_end_matches('0').trim_end_matches('.');
        trimmed.to_string()
    }
}

fn get_btn_label(idx: usize) -> &'static str {
    const LABELS: [&str; 20] = [
        "C", "+/-", "%", "/",
        "7", "8", "9", "*",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", ".", "DEL", "="
    ];
    if idx < 20 { LABELS[idx] } else { "" }
}

fn main() {
    let win_width = 360;
    let win_height = 560;

    let max_frames = if env::var("TAMARIN_CALC_BENCH").is_ok() {
        unsafe { SetConfigFlags(FLAG_WINDOW_HIDDEN); }
        300
    } else {
        0
    };

    let title = CString::new("Rust GUI Calculator").unwrap();
    unsafe {
        InitWindow(win_width, win_height, title.as_ptr());
        SetTargetFPS(60);
    }

    let mut state = CalcState::new();

    // Exact color palette
    let col_bg        = Color { r: 30, g: 30, b: 46, a: 255 };
    let col_disp_bg   = Color { r: 24, g: 24, b: 37, a: 255 };
    let col_btn_num   = Color { r: 49, g: 50, b: 68, a: 255 };
    let col_btn_hover = Color { r: 69, g: 71, b: 90, a: 255 };
    let col_btn_top   = Color { r: 59, g: 62, b: 82, a: 255 };
    let col_btn_op    = Color { r: 108, g: 92, b: 231, a: 255 };
    let col_btn_op_hov= Color { r: 162, g: 155, b: 254, a: 255 };
    let col_btn_eq    = Color { r: 0, g: 184, b: 148, a: 255 };
    let col_btn_eq_hov= Color { r: 85, g: 239, b: 196, a: 255 };
    let col_btn_del   = Color { r: 214, g: 48, b: 49, a: 255 };
    let col_btn_del_hov= Color { r: 255, g: 118, b: 117, a: 255 };
    let col_text_white= Color { r: 240, g: 240, b: 245, a: 255 };
    let col_text_muted= Color { r: 147, g: 153, b: 178, a: 255 };
    let col_text_dark = Color { r: 20, g: 25, b: 35, a: 255 };
    let col_border    = Color { r: 49, g: 50, b: 68, a: 255 };

    let mut frame_count = 0;
    while unsafe { !WindowShouldClose() } {
        if max_frames > 0 && frame_count >= max_frames {
            break;
        }
        frame_count += 1;

        let mouse = unsafe { GetMousePosition() };
        let mouse_clicked = unsafe { IsMouseButtonPressed(MOUSE_BUTTON_LEFT) };

        // Keyboard Shortcuts
        unsafe {
            if IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0) { state.append_digit('0'); }
            if IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1) { state.append_digit('1'); }
            if IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2) { state.append_digit('2'); }
            if IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3) { state.append_digit('3'); }
            if IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4) { state.append_digit('4'); }
            if IsKeyPressed(KEY_FIVE) || IsKeyPressed(KEY_KP_5) { state.append_digit('5'); }
            if IsKeyPressed(KEY_SIX) || IsKeyPressed(KEY_KP_6) { state.append_digit('6'); }
            if IsKeyPressed(KEY_SEVEN) || IsKeyPressed(KEY_KP_7) { state.append_digit('7'); }
            if IsKeyPressed(KEY_EIGHT) || IsKeyPressed(KEY_KP_8) { state.append_digit('8'); }
            if IsKeyPressed(KEY_NINE) || IsKeyPressed(KEY_KP_9) { state.append_digit('9'); }
            if IsKeyPressed(KEY_PERIOD) || IsKeyPressed(KEY_KP_DECIMAL) { state.append_dot(); }
            if IsKeyPressed(KEY_KP_ADD) { state.set_operator('+'); }
            if IsKeyPressed(KEY_KP_SUBTRACT) || IsKeyPressed(KEY_MINUS) { state.set_operator('-'); }
            if IsKeyPressed(KEY_KP_MULTIPLY) { state.set_operator('*'); }
            if IsKeyPressed(KEY_KP_DIVIDE) || IsKeyPressed(KEY_SLASH) { state.set_operator('/'); }
            if IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_EQUAL) { state.equals(); }
            if IsKeyPressed(KEY_BACKSPACE) { state.backspace(); }
            if IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_C) { state.reset(); }
        }

        unsafe {
            BeginDrawing();
            ClearBackground(col_bg);

            // 1. Draw Display Screen
            let disp_rec = Rectangle { x: 16.0, y: 16.0, width: 328.0, height: 100.0 };
            DrawRectangleRec(disp_rec, col_disp_bg);
            DrawRectangleLinesEx(disp_rec, 2.0, col_border);

            // Top expression line
            if !state.expr.is_empty() {
                let expr_c = CString::new(state.expr.as_str()).unwrap_or_default();
                let expr_w = MeasureText(expr_c.as_ptr(), 18);
                DrawText(expr_c.as_ptr(), 330 - expr_w, 26, 18, col_text_muted);
            }

            // Main value line
            let disp_c = CString::new(state.display.as_str()).unwrap_or_default();
            let text_w = MeasureText(disp_c.as_ptr(), 36);
            DrawText(disp_c.as_ptr(), 330 - text_w, 60, 36, col_text_white);

            // 2. Draw 5x4 Button Grid
            let btn_w: f32 = 74.0;
            let btn_h: f32 = 68.0;
            let start_x: f32 = 16.0;
            let start_y: f32 = 132.0;
            let gap_x: f32 = 10.0;
            let gap_y: f32 = 12.0;

            for row in 0..5 {
                for col in 0..4 {
                    let idx = row * 4 + col;
                    let bx = start_x + (col as f32) * (btn_w + gap_x);
                    let by = start_y + (row as f32) * (btn_h + gap_y);
                    let b_rec = Rectangle { x: bx, y: by, width: btn_w, height: btn_h };

                    let hov = CheckCollisionPointRec(mouse, b_rec);

                    let (cur_col, cur_txt) = match idx {
                        0 | 1 | 2 => (if hov { col_btn_hover } else { col_btn_top }, col_text_white),
                        3 | 7 | 11 | 15 => (if hov { col_btn_op_hov } else { col_btn_op }, col_text_white),
                        18 => (if hov { col_btn_del_hov } else { col_btn_del }, col_text_white),
                        19 => (if hov { col_btn_eq_hov } else { col_btn_eq }, col_text_dark),
                        _ => (if hov { col_btn_hover } else { col_btn_num }, col_text_white),
                    };

                    DrawRectangleRec(b_rec, cur_col);

                    let lbl = get_btn_label(idx);
                    let lbl_c = CString::new(lbl).unwrap_or_default();
                    let lbl_w = MeasureText(lbl_c.as_ptr(), 22);
                    let lbl_x = bx as c_int + (btn_w as c_int - lbl_w) / 2;
                    let lbl_y = by as c_int + (btn_h as c_int - 22) / 2;
                    DrawText(lbl_c.as_ptr(), lbl_x, lbl_y, 22, cur_txt);

                    if hov && mouse_clicked {
                        match idx {
                            0 => state.reset(),
                            1 => state.toggle_sign(),
                            2 => state.percent(),
                            3 => state.set_operator('/'),
                            4 => state.append_digit('7'),
                            5 => state.append_digit('8'),
                            6 => state.append_digit('9'),
                            7 => state.set_operator('*'),
                            8 => state.append_digit('4'),
                            9 => state.append_digit('5'),
                            10 => state.append_digit('6'),
                            11 => state.set_operator('-'),
                            12 => state.append_digit('1'),
                            13 => state.append_digit('2'),
                            14 => state.append_digit('3'),
                            15 => state.set_operator('+'),
                            16 => state.append_digit('0'),
                            17 => state.append_dot(),
                            18 => state.backspace(),
                            19 => state.equals(),
                            _ => {}
                        }
                    }
                }
            }

            EndDrawing();
        }
    }

    unsafe { CloseWindow(); }
}
