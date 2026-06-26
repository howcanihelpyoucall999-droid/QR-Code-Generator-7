# Passport Photo Maker

A single-page passport photo editor that:
- uploads an image,
- removes the background with ClearBackdrop,
- falls back to a Vercel proxy if the browser hits CORS,
- lets the user pick a background color,
- exports passport-size photos and A4 sheets.

## Files
- `index.html`
- `style.css`
- `script.js`
- `api/remove-bg.js`
- `vercel.json`

## Deploy on Vercel
1. Upload this folder to GitHub.
2. Import it in Vercel.
3. Deploy.

The fallback endpoint will be available at `/api/remove-bg` after deployment.
