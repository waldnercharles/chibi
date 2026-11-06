const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });
export const MAX_SOURCE_BYTES = 400000;
export const MAX_OPTIONS_BYTES = 32768;
const optionValue = (key, value) => key === 'data_column_alignment'
  ? /^(?:auto|left|right)(?:,(?:auto|left|right)){0,31}$/.test(value)
  : /^-?[a-z0-9_]+$/.test(value);

export function encodeRequest(source, options) {
  if (typeof source !== 'string' || !options || typeof options !== 'object' || Array.isArray(options)) {
    throw new Error('Invalid formatter request');
  }
  const text = encoder.encode(source);
  if (text.length > MAX_SOURCE_BYTES) throw new Error('Source exceeds 400,000 UTF-8 bytes');
  const pairs = Object.entries(options).map(([key, value]) => {
    if (!/^[a-z][a-z0-9_]*$/.test(key) ||
        !['string', 'number', 'boolean'].includes(typeof value) ||
        !optionValue(key, String(value))) throw new Error('Invalid formatter option');
    return `${key}=${value}`;
  });
  const header = encoder.encode(pairs.join(' '));
  if (header.length > MAX_OPTIONS_BYTES) throw new Error('Formatter options exceed 32,768 bytes');
  const bytes = new Uint8Array(header.length + 1 + text.length);
  bytes.set(header);
  bytes[header.length] = 10;
  bytes.set(text, header.length + 1);
  return bytes;
}

// The worker reads the existing frame instead of sending a second copy of source
// and options in its message. Source remains opaque to this transport layer.
export function decodeRequest(bytes) {
  const newline = bytes.indexOf(10);
  if (newline < 0 || newline > MAX_OPTIONS_BYTES || bytes.length - newline - 1 > MAX_SOURCE_BYTES) {
    throw new Error('Invalid formatter request frame');
  }
  const options = Object.create(null);
  const header = decoder.decode(bytes.subarray(0, newline));
  if (header !== '') {
    for (const pair of header.split(' ')) {
      const match = /^([a-z][a-z0-9_]*)=(.+)$/.exec(pair);
      if (!match || !optionValue(match[1], match[2])) throw new Error('Invalid formatter request options');
      options[match[1]] = match[2];
    }
  }
  return { source: decoder.decode(bytes.subarray(newline + 1)), options };
}

// This command is consumed by the same authored parser as formatter requests.
export function encodeImports(source) {
  const request = encodeRequest(source, {});
  const prefix = encoder.encode('!imports');
  const bytes = new Uint8Array(prefix.length + request.length);
  bytes.set(prefix);
  bytes.set(request, prefix.length);
  return bytes;
}

export function contextPath(reference) {
  if (reference === undefined || reference === null) return null;
  if (typeof reference !== 'object' || Array.isArray(reference) || typeof reference.path !== 'string') {
    throw new Error('Invalid formatter project reference');
  }
  // Reuse the static-source path contract. A relative base avoids depending on
  // location in Node tests or on the browser page's current URL.
  sourceUrl(reference.path, 'https://formatter.invalid/');
  if (!reference.path.endsWith('.chibi') || encoder.encode(reference.path).length >= 4096) {
    throw new Error('Invalid formatter project reference');
  }
  return reference.path;
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
