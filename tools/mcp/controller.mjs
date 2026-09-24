// Keeps 64-bit IDs and hashes exact: integers beyond 2^53 become decimal strings.
export function parseControllerJson(text) {
  return JSON.parse(text, (key, value, context) =>
    typeof value === "number" && !Number.isSafeInteger(value) && /^-?\d+$/.test(context.source)
      ? context.source
      : value);
}
