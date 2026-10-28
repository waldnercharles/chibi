export const MAX_SOURCE_BYTES = 256 * 1024;
export const MAX_LINK_LENGTH = 16384;
export const DEFAULTS = Object.freeze({
    indent_width: 4,
    preferred_width: 0,
    use_tabs: false,
    remove_optional_semicolons: false,
});

export function options(value) {
    if (!value || Array.isArray(value) || typeof value !== 'object' ||
        Object.keys(value).length !== 4 ||
        Object.keys(value).some(key => !Object.hasOwn(DEFAULTS, key)) ||
        !Number.isInteger(value.indent_width) || value.indent_width < 1 || value.indent_width > 16 ||
        !Number.isInteger(value.preferred_width) || value.preferred_width < 0 || value.preferred_width > 4096 ||
        typeof value.use_tabs !== 'boolean' || typeof value.remove_optional_semicolons !== 'boolean') {
        throw new Error('Use an indent width of 1–16, a preferred width of 0–4096, and boolean options.');
    }
    return { ...value };
}

export function sourceBytes(source) {
    if (typeof source !== 'string' || source.includes('\0')) {
        throw new Error('Source must be text without NUL characters.');
    }
    if (source.length > MAX_SOURCE_BYTES) {
        throw new Error('This playground accepts up to 256 KiB of UTF-8 source. Use the CLI for larger files.');
    }
    const bytes = new TextEncoder().encode(source);
    if (new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(bytes) !== source) {
        throw new Error('Source contains an incomplete Unicode character.');
    }
    if (bytes.length > MAX_SOURCE_BYTES) {
        throw new Error('This playground accepts up to 256 KiB of UTF-8 source. Use the CLI for larger files.');
    }
    return bytes;
}

export function configuration(value) {
    const selected = options(value);
    return Object.keys(DEFAULTS).map(key => `${key} = ${selected[key]}\n`).join('');
}

function revision(value) {
    if (typeof value !== 'string' || !/^[0-9a-f]{40}$/.test(value)) {
        throw new Error('The link does not identify a valid formatter revision.');
    }
    return value;
}

export function shareLink(base, engine, selected, source) {
    const value = { v: 1, engine: revision(engine), options: options(selected) };
    if (source !== undefined) {
        sourceBytes(source);
        value.source = source;
    }
    const bytes = new TextEncoder().encode(JSON.stringify(value));
    // Reject oversized input before building the base64 string. The final URL
    // check also accounts for a long deployment path or query string.
    if (bytes.length > MAX_LINK_LENGTH) {
        throw new Error('Source is too large for a share link. Share settings only, or download the source.');
    }
    const encoded = btoa(Array.from(bytes, byte => String.fromCharCode(byte)).join(''))
        .replaceAll('+', '-').replaceAll('/', '_').replace(/=+$/, '');
    const url = new URL(base);
    url.search = '';
    url.hash = 'format=' + encoded;
    if (url.href.length > MAX_LINK_LENGTH) {
        throw new Error('This share link exceeds 16,384 characters. Share settings only, or download the source.');
    }
    return url.href;
}

export function readLink(hash) {
    if (!hash.startsWith('#format=')) return null;
    const encoded = hash.slice(8);
    if (hash.length > MAX_LINK_LENGTH || !/^[A-Za-z0-9_-]+$/.test(encoded)) {
        throw new Error('This formatter link is malformed or too large. Your current input has not changed.');
    }
    let value;
    try {
        const decoded = atob(encoded.replaceAll('-', '+').replaceAll('_', '/'));
        const bytes = Uint8Array.from(decoded, letter => letter.charCodeAt(0));
        value = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
    } catch {
        throw new Error('This formatter link cannot be decoded. Your current input has not changed.');
    }
    if (!value || typeof value !== 'object' || Array.isArray(value) || value.v !== 1 ||
        Object.keys(value).some(key => !['v', 'engine', 'options', 'source'].includes(key))) {
        throw new Error('This formatter link uses an unsupported format. Your current input has not changed.');
    }
    const selected = options(value.options);
    const engine = revision(value.engine);
    if (Object.hasOwn(value, 'source')) sourceBytes(value.source);
    return { engine, options: selected, source: value.source };
}
