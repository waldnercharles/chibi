const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });
export const MAX_SOURCE_BYTES = 400000;
const MAX_OPTIONS_BYTES = 4096;

export function encodeRequest(source, options) {
  if (typeof source !== 'string' || !options || typeof options !== 'object' || Array.isArray(options)) {
    throw new Error('Invalid formatter request');
  }
  const text = encoder.encode(source);
  if (text.length > MAX_SOURCE_BYTES) throw new Error('Source exceeds 400,000 UTF-8 bytes');
  const pairs = Object.entries(options).map(([key, value]) => {
    if (!/^[a-z][a-z0-9_]*$/.test(key) ||
        !['string', 'number', 'boolean'].includes(typeof value) ||
        !/^-?[a-z0-9_]+$/.test(String(value))) throw new Error('Invalid formatter option');
    return `${key}=${value}`;
  });
  const header = encoder.encode(pairs.join(' '));
  if (header.length > MAX_OPTIONS_BYTES) throw new Error('Formatter options exceed 4,096 bytes');
  const bytes = new Uint8Array(header.length + 1 + text.length);
  bytes.set(header);
  bytes[header.length] = 10;
  bytes.set(text, header.length + 1);
  return bytes;
}

// Lengths count UTF-8 bytes, including embedded NULs and line breaks in a part.
export function decodeResult(bytes) {
  const first = bytes.indexOf(10);
  const second = bytes.indexOf(10, first + 1);
  if (first < 0 || first > 5 || second < 0 || second - first > 64) throw new Error('Invalid formatter response header');
  const status = decoder.decode(bytes.subarray(0, first));
  const line = decoder.decode(bytes.subarray(first + 1, second));
  if (!['ok', 'error'].includes(status) || !/^\d+ \d+ \d+( \d+)?$/.test(line)) throw new Error('Invalid formatter response header');
  const lengths = line.split(' ').map(Number);
  if (lengths.some((value) => !Number.isSafeInteger(value)) ||
      lengths.reduce((sum, value) => sum + value, second + 1) !== bytes.length) {
    throw new Error('Invalid formatter response lengths');
  }
  let offset = second + 1;
  const parts = lengths.map((length) => {
    const text = decoder.decode(bytes.subarray(offset, offset + length));
    offset += length;
    return text;
  });
  const result = status === 'ok'
    ? { ok: true, text: parts[0], original_html: parts[1], formatted_html: parts[2] }
    : { ok: false, error: parts[0], original_html: parts[1] };
  if (parts.length === 4) result.notes = parts[3];
  return result;
}

export function sourceUrl(path, base) {
  if (typeof path !== 'string' || /[\\\0]/.test(path) ||
      path.split('/').some((part) => part === '' || part === '.' || part === '..')) {
    throw new Error('Invalid source path');
  }
  return new URL('sources/' + path.split('/').map(encodeURIComponent).join('/'), base);
}
