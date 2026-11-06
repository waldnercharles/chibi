// Node design study: renders node mockups from small specs with the graph
// canvas's metrics (graph.rcss), so proposals compare against today's nodes.
const TYPES = {
  exec: '#e8e8ea', bool: '#f26e7a', int: '#03c46d', float: '#a6d963', vec: '#6694eb',
  entity: '#5cc4d9', name: '#ba91e6', string: '#e87aba', any: '#a3a3a8',
};

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function socket(pin) {
  const s = el('span', 'socket ' + (pin.type === 'exec' ? 'exec' : pin.shape || 'data'));
  const color = TYPES[pin.type] || TYPES.any;
  s.style.setProperty('--c', color);
  if (!pin.wired) s.classList.add('hollow');
  return s;
}

function value(v) {
  if (v.kind === 'number') {
    const f = el('span', 'field');
    if (v.addon) f.append(el('span', 'addon', v.addon));
    f.append(el('span', 'num', v.text));
    return f;
  }
  if (v.kind === 'vector') {
    const w = el('span', 'vector');
    v.axes.forEach(([a, t]) => { const f = el('span', 'field'); f.append(el('span', 'addon ' + a.toLowerCase(), a), el('span', 'num', t)); w.append(f); });
    return w;
  }
  if (v.kind === 'check') return el('span', 'check' + (v.on ? ' on' : ''));
  if (v.kind === 'choice') return el('span', 'field select', v.text);
  const chip = el('span', 'chip ' + (v.source || ''));
  if (v.icon) chip.append(el('i', 'ti ti-' + v.icon));
  chip.append(el('span', '', v.text));
  if (v.dim) chip.append(el('span', 'dim', v.dim));
  return chip;
}

function row(pin, labelWidth) {
  const r = el('div', 'grow ' + pin.side);
  if (pin.side === 'exec') {
    const inHalf = el('span', 'half in'), outHalf = el('span', 'half out');
    if (pin.in) { inHalf.append(socket({ type: 'exec', wired: pin.in.wired })); if (pin.in.label) inHalf.append(el('span', 'label', pin.in.label)); }
    if (pin.out) { if (pin.out.label) outHalf.append(el('span', 'label', pin.out.label)); outHalf.append(socket({ type: 'exec', wired: pin.out.wired })); }
    r.append(inHalf, outHalf);
    return r;
  }
  r.append(socket(pin));
  if (pin.label !== undefined) {
    const l = el('span', 'label', pin.label);
    if (pin.side === 'in' && labelWidth) l.style.width = labelWidth + 'px';
    r.append(l);
  }
  if (pin.value) r.append(value(pin.value));
  if (pin.side === 'out') r.append(r.firstChild);
  return r;
}

export function node(spec) {
  const n = el('div', 'gnode ' + (spec.variant || ''));
  n.dataset.kind = spec.kind || 'pure';
  if (spec.width) n.style.width = spec.width + 'px';
  if (spec.compact) {
    n.classList.add('compact');
    const body = el('div', 'compact-body');
    (spec.rows || []).forEach(p => body.append(row(p)));
    body.append(el('span', 'symbol', spec.symbol));
    n.append(body);
    return n;
  }
  const head = el('div', 'head');
  if (spec.headIn) head.append(socket({ type: 'exec', wired: spec.headIn.wired }));
  if (spec.icon) head.append(el('i', 'ti ti-' + spec.icon));
  head.append(el('span', 'title', spec.title));
  if (spec.headOut) head.append(socket({ type: 'exec', wired: spec.headOut.wired }));
  n.append(head);
  const body = el('div', 'body');
  if (spec.code) {
    const code = el('div', 'code');
    code.innerHTML = spec.code;
    body.append(code);
  }
  (spec.rows || []).forEach(p => body.append(row(p, spec.labelWidth)));
  n.append(body);
  return n;
}

export function mount(id, specs) {
  const host = document.getElementById(id);
  specs.forEach(s => host.append(node(s)));
}
