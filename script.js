const API_URL = "https://clearbackdrop.com/api/v1/remove-background";
const PROXY_URL = "/api/remove-bg"; // change to your free proxy endpoint if needed

const els = {
  imageInput: document.getElementById("imageInput"),
  bgColor: document.getElementById("bgColor"),
  bgHex: document.getElementById("bgHex"),
  sizePreset: document.getElementById("sizePreset"),
  widthInput: document.getElementById("widthInput"),
  heightInput: document.getElementById("heightInput"),
  copiesInput: document.getElementById("copiesInput"),
  fitHead: document.getElementById("fitHead"),
  enhancePhoto: document.getElementById("enhancePhoto"),
  processBtn: document.getElementById("processBtn"),
  downloadBtn: document.getElementById("downloadBtn"),
  downloadSheetBtn: document.getElementById("downloadSheetBtn"),
  canvas: document.getElementById("canvas"),
  statusText: document.getElementById("statusText"),
  progressBar: document.getElementById("progressBar").querySelector("span"),
  previewInfo: document.getElementById("previewInfo"),
  apiLabel: document.getElementById("apiLabel"),
  proxyLabel: document.getElementById("proxyLabel"),
};

const ctx = els.canvas.getContext("2d");
let originalFile = null;
let cutoutImage = null;
let lastResultBlob = null;
let lastSheetBlob = null;
let objectUrlToRevoke = null;

els.apiLabel.textContent = API_URL;
els.proxyLabel.textContent = PROXY_URL;

function setStatus(text, progress = 0) {
  els.statusText.textContent = text;
  els.progressBar.style.width = `${progress}%`;
}

function isValidHex(value) {
  return /^#([0-9a-fA-F]{6})$/.test(value);
}

function syncColorInputs(value) {
  if (!isValidHex(value)) return;
  els.bgColor.value = value;
  els.bgHex.value = value;
  redraw();
}

els.bgColor.addEventListener("input", () => syncColorInputs(els.bgColor.value));
els.bgHex.addEventListener("change", () => {
  const v = els.bgHex.value.trim();
  if (isValidHex(v)) syncColorInputs(v);
});

document.querySelectorAll(".swatch").forEach(btn => {
  btn.addEventListener("click", () => syncColorInputs(btn.dataset.color));
});

els.sizePreset.addEventListener("change", () => {
  const preset = els.sizePreset.value;
  if (preset === "35x45") {
    els.widthInput.value = 35;
    els.heightInput.value = 45;
  } else if (preset === "2x2") {
    els.widthInput.value = 51;
    els.heightInput.value = 51;
  }
  redraw();
});

[els.widthInput, els.heightInput, els.copiesInput, els.fitHead, els.enhancePhoto].forEach(el => {
  el.addEventListener("input", redraw);
  el.addEventListener("change", redraw);
});

els.imageInput.addEventListener("change", async () => {
  const file = els.imageInput.files && els.imageInput.files[0];
  if (!file) return;
  originalFile = file;
  setStatus("Image selected. Ready to remove background.", 8);
  els.previewInfo.textContent = file.name;
  await processImage();
});

els.processBtn.addEventListener("click", processImage);
els.downloadBtn.addEventListener("click", downloadCutout);
els.downloadSheetBtn.addEventListener("click", downloadSheet);

async function processImage() {
  if (!originalFile) {
    setStatus("Choose an image first.", 0);
    return;
  }

  try {
    setStatus("Uploading to ClearBackdrop...", 18);
    const blob = await removeBackgroundWithFallback(originalFile);
    lastResultBlob = blob;
    cutoutImage = await blobToImage(blob);
    setStatus("Background removed successfully.", 72);
    redraw();
    els.downloadBtn.disabled = false;
    els.downloadSheetBtn.disabled = false;
    setStatus("Done. You can download now.", 100);
  } catch (error) {
    console.error(error);
    setStatus("Removal failed. Check the proxy or API response.", 0);
    alert(`Background removal failed: ${error.message}`);
  }
}

async function removeBackgroundWithFallback(file) {
  const formData = new FormData();
  formData.append("image", file);

  try {
    const direct = await fetch(API_URL, {
      method: "POST",
      body: formData,
    });

    if (!direct.ok) {
      throw new Error(`Direct API error ${direct.status}`);
    }

    const contentType = direct.headers.get("content-type") || "";
    if (contentType.includes("application/json")) {
      const data = await direct.json();
      const url = data.result_url || data.url;
      if (!url) throw new Error("JSON response did not include an image URL");
      const imgResponse = await fetch(url);
      if (!imgResponse.ok) throw new Error("Could not fetch image from result URL");
      return await imgResponse.blob();
    }

    return await direct.blob();
  } catch (directError) {
    console.warn("Direct request failed, trying proxy...", directError);

    if (!PROXY_URL || PROXY_URL === "/api/remove-bg") {
      // continue anyway; the endpoint can be replaced later
    }

    const proxyForm = new FormData();
    proxyForm.append("image", file);

    const proxied = await fetch(PROXY_URL, {
      method: "POST",
      body: proxyForm,
    });

    if (!proxied.ok) {
      throw new Error(`Proxy error ${proxied.status}`);
    }

    const contentType = proxied.headers.get("content-type") || "";
    if (contentType.includes("application/json")) {
      const data = await proxied.json();
      const url = data.result_url || data.url;
      if (!url) throw new Error("Proxy JSON response did not include an image URL");
      const imgResponse = await fetch(url);
      if (!imgResponse.ok) throw new Error("Could not fetch image from proxy result URL");
      return await imgResponse.blob();
    }

    return await proxied.blob();
  }
}

function blobToImage(blob) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(blob);
    const img = new Image();
    img.onload = () => {
      URL.revokeObjectURL(url);
      resolve(img);
    };
    img.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error("Could not decode the returned image"));
    };
    img.src = url;
  });
}

function mmToPx(mm, dpi = 300) {
  return Math.round((mm / 25.4) * dpi);
}

function enhanceCanvas(canvas, amount = 1.08) {
  const tmp = document.createElement("canvas");
  tmp.width = canvas.width;
  tmp.height = canvas.height;
  const tctx = tmp.getContext("2d");
  tctx.drawImage(canvas, 0, 0);

  const image = tctx.getImageData(0, 0, tmp.width, tmp.height);
  const data = image.data;
  const contrast = amount;
  const brightness = 6;

  for (let i = 0; i < data.length; i += 4) {
    for (let c = 0; c < 3; c++) {
      let v = data[i + c];
      v = (v - 128) * contrast + 128 + brightness;
      data[i + c] = Math.max(0, Math.min(255, v));
    }
  }

  tctx.putImageData(image, 0, 0);
  return tmp;
}

function drawRoundedImage(ctx, img, x, y, w, h, radius = 24) {
  ctx.save();
  ctx.beginPath();
  ctx.moveTo(x + radius, y);
  ctx.arcTo(x + w, y, x + w, y + h, radius);
  ctx.arcTo(x + w, y + h, x, y + h, radius);
  ctx.arcTo(x, y + h, x, y, radius);
  ctx.arcTo(x, y, x + w, y, radius);
  ctx.closePath();
  ctx.clip();
  ctx.drawImage(img, x, y, w, h);
  ctx.restore();
}

function redraw() {
  const bg = els.bgColor.value;
  const copies = Math.max(1, Math.min(40, parseInt(els.copiesInput.value || "1", 10)));
  const widthMM = Math.max(20, parseInt(els.widthInput.value || "35", 10));
  const heightMM = Math.max(20, parseInt(els.heightInput.value || "45", 10));

  els.copiesInput.value = copies;
  els.widthInput.value = widthMM;
  els.heightInput.value = heightMM;

  const pageW = mmToPx(210);
  const pageH = mmToPx(297);
  els.canvas.width = pageW;
  els.canvas.height = pageH;

  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, pageW, pageH);

  if (!cutoutImage) {
    ctx.fillStyle = "#1b2239";
    ctx.font = "bold 42px system-ui";
    ctx.textAlign = "center";
    ctx.fillText("Preview will appear here", pageW / 2, pageH / 2 - 10);
    ctx.font = "24px system-ui";
    ctx.fillStyle = "#64748b";
    ctx.fillText("Upload an image to begin", pageW / 2, pageH / 2 + 36);
    els.previewInfo.textContent = "A4 preview";
    return;
  }

  const sourceCanvas = document.createElement("canvas");
  sourceCanvas.width = cutoutImage.naturalWidth;
  sourceCanvas.height = cutoutImage.naturalHeight;
  const sctx = sourceCanvas.getContext("2d");
  sctx.drawImage(cutoutImage, 0, 0);

  const finalSource = els.enhancePhoto.checked ? enhanceCanvas(sourceCanvas) : sourceCanvas;
  const img = new Image();
  img.src = finalSource.toDataURL("image/png");

  const photoW = mmToPx(widthMM);
  const photoH = mmToPx(heightMM);

  const margin = mmToPx(8);
  const gap = mmToPx(4);
  const cols = Math.max(1, Math.floor((pageW - margin * 2 + gap) / (photoW + gap)));
  const rows = Math.max(1, Math.floor((pageH - margin * 2 + gap) / (photoH + gap)));
  const maxCopies = cols * rows;
  const finalCopies = Math.min(copies, maxCopies);

  const startX = Math.round((pageW - (Math.min(finalCopies, cols) * photoW + (Math.min(finalCopies, cols) - 1) * gap)) / 2);
  const startY = margin;

  const render = () => {
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, pageW, pageH);

    ctx.strokeStyle = "rgba(0,0,0,0.08)";
    ctx.lineWidth = 2;

    for (let i = 0; i < finalCopies; i++) {
      const col = i % cols;
      const row = Math.floor(i / cols);
      if (row >= rows) break;
      const x = startX + col * (photoW + gap);
      const y = startY + row * (photoH + gap);

      // Fill photo background
      ctx.fillStyle = bg;
      ctx.fillRect(x, y, photoW, photoH);

      // Draw subject centered and scaled
      const iw = img.width || finalSource.width;
      const ih = img.height || finalSource.height;
      const scale = Math.min(photoW / iw, photoH / ih) * 0.92;
      const dw = iw * scale;
      const dh = ih * scale;
      const dx = x + (photoW - dw) / 2;
      const dy = y + (photoH - dh) / 2;

      ctx.save();
      ctx.beginPath();
      ctx.rect(x, y, photoW, photoH);
      ctx.clip();
      ctx.drawImage(finalSource, dx, dy, dw, dh);
      ctx.restore();

      ctx.strokeStyle = "rgba(0,0,0,0.10)";
      ctx.strokeRect(x + 0.5, y + 0.5, photoW - 1, photoH - 1);
    }

    els.previewInfo.textContent = `${finalCopies} copy${finalCopies > 1 ? "ies" : ""} on A4`;
  };

  if (img.complete) {
    render();
  } else {
    img.onload = render;
  }
}

async function downloadCutout() {
  if (!lastResultBlob) return;
  const url = URL.createObjectURL(lastResultBlob);
  triggerDownload(url, "background-removed.png");
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}

async function downloadSheet() {
  redraw();
  const blob = await new Promise(resolve => els.canvas.toBlob(resolve, "image/png"));
  if (!blob) return;
  lastSheetBlob = blob;
  const url = URL.createObjectURL(blob);
  triggerDownload(url, "passport-sheet-a4.png");
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}

function triggerDownload(url, filename) {
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
}

function setInitialCanvas() {
  redraw();
  setStatus("Waiting for an image", 0);
}

setInitialCanvas();
