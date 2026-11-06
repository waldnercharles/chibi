// Line diff by Myers' O(ND) shortest edit script. diffLines returns the line indexes that have no
// counterpart on the other side, or null when the texts differ too much; diffOps lists the edit
// script in order, for a unified view.
function diffLines(a, b) {
  const n = a.length, m = b.length, max = n + m;
  if (max === 0) return { a: new Set(), b: new Set() };
  const offset = max, trace = [];
  const v = new Int32Array(2 * max + 2);
  let found = -1;
  for (let d = 0; d <= max && found < 0; d++) {
    // Each round keeps a copy of the frontier, so a rewrite of the whole file costs O(D^2) memory.
    if (d > 3000) return null;
    trace.push(v.slice());
    for (let k = -d; k <= d; k += 2) {
      let x = (k === -d || (k !== d && v[offset + k - 1] < v[offset + k + 1])) ? v[offset + k + 1] : v[offset + k - 1] + 1;
      let y = x - k;
      while (x < n && y < m && a[x] === b[y]) { x++; y++; }
      v[offset + k] = x;
      if (x >= n && y >= m) { found = d; break; }
    }
  }
  const onlyA = new Set(), onlyB = new Set();
  let x = n, y = m;
  for (let d = found; d > 0; d--) {
    const prev = trace[d], k = x - y;
    const down = (k === -d || (k !== d && prev[offset + k - 1] < prev[offset + k + 1]));
    const pk = down ? k + 1 : k - 1;
    const px = prev[offset + pk], py = px - pk;
    while (x > px && y > py) { x--; y--; }
    if (down) onlyB.add(py); else onlyA.add(px);
    x = px; y = py;
  }
  return { a: onlyA, b: onlyB };
}

// The edit script as rows {kind: '=' | '-' | '+', a, b}: a and b are line indexes on each side (-1 for none).
function diffOps(a, b, diff) {
  const ops = [];
  let i = 0, j = 0;
  while (i < a.length || j < b.length) {
    if (i < a.length && diff.a.has(i)) ops.push({ kind: '-', a: i++, b: -1 });
    else if (j < b.length && diff.b.has(j)) ops.push({ kind: '+', a: -1, b: j++ });
    else ops.push({ kind: '=', a: i++, b: j++ });
  }
  return ops;
}
