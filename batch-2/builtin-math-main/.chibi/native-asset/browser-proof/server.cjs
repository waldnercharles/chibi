const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const directory = __dirname;
const server = http.createServer((request, response) => {
  const name = new URL(request.url, 'http://127.0.0.1').pathname;
  const file = name === '/' ? 'index.html' : name.slice(1);
  if (path.basename(file) !== file) { response.writeHead(404).end(); return; }
  fs.readFile(path.join(directory, file), (error, bytes) => {
    if (error) { response.writeHead(404).end(); return; }
    response.writeHead(200, {
      'Content-Type': file.endsWith('.wasm') ? 'application/wasm' : file.endsWith('.js') ? 'text/javascript' : 'text/html',
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp'
    });
    response.end(bytes);
  });
});
server.listen(0, '127.0.0.1', () => process.stdout.write(String(server.address().port) + '\n'));
