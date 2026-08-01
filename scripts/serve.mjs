// Static file server for local testing of the staged web/ tree.
// Node rather than python3 -m http.server: the sandbox blocks getcwd(), and
// the paks need byte-range support for the loader's progress reporting.

import { createServer } from 'node:http';
import { createReadStream, statSync } from 'node:fs';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';

const PORT = Number(process.argv[2] || 8666);
// optional third arg serves some other tree, e.g. an unpacked .xdc
const ROOT = process.argv[3]
  ? process.argv[3]
  : join(fileURLToPath(new URL('.', import.meta.url)), '..', 'web');

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.pk3': 'application/octet-stream',
  '.css': 'text/css; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
};

createServer((req, res) => {
  let rel = decodeURIComponent(req.url.split('?')[0]);
  if (rel === '/') rel = '/index.html';

  // normalize before joining so ../ cannot climb out of web/
  const path = join(ROOT, normalize(rel).replace(/^(\.\.[/\\])+/, ''));

  let st;
  try {
    st = statSync(path); // follows the symlinks stage-web.sh creates
  } catch {
    res.writeHead(404, { 'content-type': 'text/plain' });
    res.end('not found\n');
    return;
  }

  const type = TYPES[extname(path)] || 'application/octet-stream';
  const range = req.headers.range;

  if (range) {
    const m = /bytes=(\d*)-(\d*)/.exec(range);
    const start = m && m[1] ? Number(m[1]) : 0;
    const end = m && m[2] ? Number(m[2]) : st.size - 1;
    res.writeHead(206, {
      'content-type': type,
      'content-length': end - start + 1,
      'content-range': `bytes ${start}-${end}/${st.size}`,
      'accept-ranges': 'bytes',
    });
    createReadStream(path, { start, end }).pipe(res);
    return;
  }

  res.writeHead(200, {
    'content-type': type,
    'content-length': st.size,
    'accept-ranges': 'bytes',
    'cache-control': 'no-cache',
  });
  createReadStream(path).pipe(res);
}).listen(PORT, () => {
  console.log(`serving ${ROOT} on http://localhost:${PORT}`);
});
