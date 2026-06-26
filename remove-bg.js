const DEFAULT_CLEARBACKDROP_URL = 'https://clearbackdrop.com/api/v1/remove-background';

export default async function handler(req, res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

  if (req.method === 'OPTIONS') {
    return res.status(200).end();
  }

  if (req.method !== 'POST') {
    return res.status(405).json({ success: false, error: 'Method not allowed' });
  }

  try {
    const chunks = [];
    for await (const chunk of req) chunks.push(chunk);
    const bodyBuffer = Buffer.concat(chunks);

    const contentType = req.headers['content-type'] || 'application/octet-stream';

    const upstream = await fetch(DEFAULT_CLEARBACKDROP_URL, {
      method: 'POST',
      headers: { 'content-type': contentType },
      body: bodyBuffer,
    });

    const upstreamType = upstream.headers.get('content-type') || '';

    if (!upstream.ok) {
      const text = await upstream.text();
      return res.status(upstream.status).send(text);
    }

    if (upstreamType.includes('application/json')) {
      const data = await upstream.json();
      return res.status(200).json(data);
    }

    const arrayBuffer = await upstream.arrayBuffer();
    res.setHeader('Content-Type', upstreamType || 'image/png');
    return res.status(200).send(Buffer.from(arrayBuffer));
  } catch (error) {
    return res.status(500).json({
      success: false,
      error: error?.message || 'Proxy error',
    });
  }
}
