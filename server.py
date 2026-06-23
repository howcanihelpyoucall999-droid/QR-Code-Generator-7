
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from PIL import Image, ImageEnhance, ImageFilter
import base64, io, re, math
import cv2
import numpy as np

app = FastAPI(title="GoBy Self-Hosted API", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

class ProcessRequest(BaseModel):
    image: str
    prompt: str | None = None
    bg_color: str = "#FFFFFF"
    width_mm: float = 35
    height_mm: float = 45
    gender: str | None = None
    outfit: str | None = None
    copies: int | None = None

def _decode_data_url(s: str) -> bytes:
    if "," in s and s.strip().startswith("data:"):
        s = s.split(",", 1)[1]
    return base64.b64decode(s)

def _to_rgb_hex(v: str) -> tuple[int, int, int]:
    if not v:
        return (255, 255, 255)
    m = re.fullmatch(r"#?([0-9a-fA-F]{6})", v.strip())
    if not m:
        return (255, 255, 255)
    h = m.group(1)
    return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))

def _auto_enhance(pil: Image.Image) -> Image.Image:
    # Mild clarity / tone boost
    img = pil.convert("RGB")
    img = ImageEnhance.Brightness(img).enhance(1.03)
    img = ImageEnhance.Contrast(img).enhance(1.08)
    img = ImageEnhance.Color(img).enhance(1.02)
    img = img.filter(ImageFilter.UnsharpMask(radius=1.2, percent=120, threshold=2))
    return img

def _grabcut_foreground(img_bgr: np.ndarray) -> np.ndarray:
    h, w = img_bgr.shape[:2]
    mask = np.zeros((h, w), np.uint8)
    bgdModel = np.zeros((1, 65), np.float64)
    fgdModel = np.zeros((1, 65), np.float64)

    # Rectangle with generous margins; works okay for portrait photos.
    rect = (int(w * 0.08), int(h * 0.06), int(w * 0.84), int(h * 0.88))
    try:
        cv2.grabCut(img_bgr, mask, rect, bgdModel, fgdModel, 5, cv2.GC_INIT_WITH_RECT)
        mask2 = np.where((mask == 2) | (mask == 0), 0, 1).astype("uint8")
        return mask2
    except Exception:
        # Fallback: assume subject is centered
        mask2 = np.zeros((h, w), np.uint8)
        x1, y1 = int(w * 0.12), int(h * 0.08)
        x2, y2 = int(w * 0.88), int(h * 0.92)
        mask2[y1:y2, x1:x2] = 1
        return mask2

def _crop_to_aspect(img: Image.Image, target_aspect: float) -> Image.Image:
    w, h = img.size
    src_aspect = w / h
    if abs(src_aspect - target_aspect) < 1e-3:
        return img

    if src_aspect > target_aspect:
        new_w = int(h * target_aspect)
        left = max(0, (w - new_w) // 2)
        return img.crop((left, 0, left + new_w, h))
    else:
        new_h = int(w / target_aspect)
        top = max(0, int((h - new_h) * 0.18))  # top bias for face framing
        top = min(top, max(0, h - new_h))
        return img.crop((0, top, w, top + new_h))

@app.get("/health")
def health():
    return {"ok": True}

@app.post("/api/process")
def process(req: ProcessRequest):
    try:
        raw = _decode_data_url(req.image)
        pil = Image.open(io.BytesIO(raw)).convert("RGB")
        np_img = cv2.cvtColor(np.array(pil), cv2.COLOR_RGB2BGR)

        # Foreground extraction
        fg_mask = _grabcut_foreground(np_img)
        fg_mask_3 = np.dstack([fg_mask] * 3)
        rgb = cv2.cvtColor(np_img, cv2.COLOR_BGR2RGB)

        # Fill background
        bg_rgb = np.array(_to_rgb_hex(req.bg_color), dtype=np.uint8).reshape(1, 1, 3)
        bg = np.tile(bg_rgb, (rgb.shape[0], rgb.shape[1], 1))
        comp = np.where(fg_mask_3 == 1, rgb, bg).astype(np.uint8)

        out = Image.fromarray(comp)

        # Face/portrait cleanup: crop to requested aspect and enhance
        target_aspect = float(req.width_mm) / float(req.height_mm)
        out = _crop_to_aspect(out, target_aspect)
        out = _auto_enhance(out)

        # Return high-res result (frontend will do final sheet layout)
        out = out.resize((out.size[0] * 2, out.size[1] * 2), Image.Resampling.LANCZOS)

        buf = io.BytesIO()
        out.save(buf, format="PNG", optimize=True)
        b64 = base64.b64encode(buf.getvalue()).decode("ascii")
        return {
            "image_b64": b64,
            "width_mm": req.width_mm,
            "height_mm": req.height_mm,
            "mode": "self-hosted"
        }

    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
