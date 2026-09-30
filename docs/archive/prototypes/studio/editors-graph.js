/* Behavior graph editor. Renders authored graphs from the fixture: nodes are
   positioned elements inside a transformed stage; wires are drawn beneath them. */
'use strict';
(() => {
const { $, $$, h, clamp } = Kit;
const TYPE_COLOR = { exec: '#e8e8ea', float: '#a3a3a8', double: '#a3a3a8', int: '#5fa05f', bool: '#d19ad6', CF_V2: '#7c7ce6', string: '#6cb2ff', entity: '#e0843c', color: '#cfc53a' };
const typeColor = (t) => TYPE_COLOR[t] || '#a3a3a8';
const KIND = (n) => (n.type === 'Event' ? 'Event' : n.type === 'Behavior Expression' ? 'Expression' : n.type === 'Branch' || n.type === 'Sequence' ? 'Branch' : /Component|Get|Set/.test(n.type) ? 'Component' : 'Call');
const KIND_ICON = { Event: 'event', Expression: 'expression', Branch: 'branch', Component: 'field', Call: 'fn' };
const graphs = {};
function behavior(name) { return (graphs[name] ??= App.clone(App.F.behaviors[name])); }
let nextNodeId = 100000;

const NODE_LIBRARY = [
  { group: 'Flow', type: 'Branch', title: 'Branch', inputs: [{ name: 'Exec', kind: 'exec', type: 'exec' }, { name: 'Condition', kind: 'data', type: 'bool', default: false }], outputs: [{ name: 'True', kind: 'exec', type: 'exec' }, { name: 'False', kind: 'exec', type: 'exec' }] },
  { group: 'Flow', type: 'Sequence', title: 'Sequence', inputs: [{ name: 'Exec', kind: 'exec', type: 'exec' }], outputs: [{ name: 'Then 0', kind: 'exec', type: 'exec' }, { name: 'Then 1', kind: 'exec', type: 'exec' }] },
  { group: 'Flow', type: 'Delay', title: 'Delay', inputs: [{ name: 'Exec', kind: 'exec', type: 'exec' }, { name: 'Seconds', kind: 'data', type: 'float', default: 0.5 }], outputs: [{ name: 'Then', kind: 'exec', type: 'exec' }] },
  { group: 'Math', type: 'Behavior Expression', title: 'x * 2', inputs: [{ name: 'Exec', kind: 'exec', type: 'exec' }, { name: 'x', kind: 'data', type: 'float', default: 0 }], outputs: [{ name: 'Then', kind: 'exec', type: 'exec' }, { name: 'Result', kind: 'data', type: 'float' }] },
  { group: 'Math', type: 'Random', title: 'Random Range', inputs: [{ name: 'Min', kind: 'data', type: 'float', default: 0 }, { name: 'Max', kind: 'data', type: 'float', default: 1 }], outputs: [{ name: 'Value', kind: 'data', type: 'float' }] },
  { group: 'Components', type: 'Get Component', title: 'Get Velocity', inputs: [], outputs: [{ name: 'x', kind: 'data', type: 'float' }, { name: 'y', kind: 'data', type: 'float' }] },
  { group: 'Components', type: 'Set Component', title: 'Set Velocity', inputs: [{ name: 'Exec', kind: 'exec', type: 'exec' }, { name: 'x', kind: 'data', type: 'float', default: 0 }, { name: 'y', kind: 'data', type: 'float', default: 0 }], outputs: [{ name: 'Then', kind: 'exec', type: 'exec' }] },
  { group: 'Entities', type: 'Spawn', title: 'Spawn Prototype', inputs: [{ name: 'Exec', kind: 'exec', type: 'exec' }, { name: 'Prototype', kind: 'data', type: 'string', default: 'Projectile' }], outputs: [{ name: 'Then', kind: 'exec', type: 'exec' }, { name: 'Entity', kind: 'data', type: 'entity' }] },
  { group: 'Entities', type: 'Destroy', title: 'Destroy', inputs: [{ name: 'Exec', kind: 'exec', type: 'exec' }, { name: 'Entity', kind: 'data', type: 'entity' }], outputs: [{ name: 'Then', kind: 'exec', type: 'exec' }] },
  { group: 'Events', type: 'Event', title: 'Damaged', inputs: [], outputs: [{ name: 'Then', kind: 'exec', type: 'exec' }, { name: 'Amount', kind: 'data', type: 'int' }] },
];

App.EDITORS.graph = (area, head, body) => {
  const view = { x: 40, y: 40, zoom: 0.9 };
  let drag = null, marquee = null, wire = null, lastPointer = { x: 0, y: 0 }, framed = null;
  head.insertAdjacentHTML('beforeend', `
    <button class="btn menu-btn" data-m="view">View</button><button class="btn menu-btn" data-m="select">Select</button><button class="btn menu-btn" data-m="add">Add</button>
    <span class="sep" style="width:1px;height:14px;background:var(--line-strong);margin:0 4px"></span>
    <div class="crumbs"></div>
    <span class="spacer"></span>
    <button class="btn frame" data-tip="Frame all" data-kbd="Home">${icon('frame')}<span>Frame</span></button>
    <button class="btn icon-only max" data-tip="Maximize area" data-kbd="Ctrl+Space">${icon('maximize')}</button>`);
  body.innerHTML = '<div class="graph"><canvas class="grid"></canvas><div class="stage"><svg class="wires"></svg><div class="nodes"></div></div><span class="zoom-readout"></span></div>';
  const root = $('.graph', body), stage = $('.stage', root), svg = $('svg', root), nodesEl = $('.nodes', root), grid = $('canvas', root);

  const current = () => behavior(App.State.graph.behavior).graphs[App.State.graph.index];
  const graphLabel = (g) => (g.kind === 'event' ? 'Event graph' : g.name);
  const docId = () => `behavior:${App.State.graph.behavior}`;
  function record(label, redo, undo, what = 'graph') {
    if (!App.doc(docId())) App.docs.push({ id: docId(), kind: 'behavior', name: App.State.graph.behavior, icon: 'graph', changes: 0, saved: 0 });
    App.history.undo.push({ label, doc: docId(), what, redo, undo });
    App.history.redo.length = 0;
    redo();
    App.doc(docId()).changes++;
    App.refresh(what);
  }

  function crumbs() {
    const c = $('.crumbs', head), e = App.activeEntity(), g = current();
    const beh = App.State.graph.behavior;
    c.innerHTML = `${e ? `<span class="row" style="gap:4px">${icon(App.protoIcon(e.prototype))}<span>${e.name}</span></span><span class="crumb-sep">›</span>` : ''}
      <button class="btn b-sel">${icon('graph')}<span class="current">${beh}</span>${icon('down', 'sm')}</button><span class="crumb-sep">›</span>
      <button class="btn g-sel">${icon(g.kind === 'event' ? 'event' : 'fn')}<span class="current">${graphLabel(g)}</span>${icon('down', 'sm')}</button>`;
    $('.b-sel', c).addEventListener('pointerdown', (ev) => {
      ev.preventDefault();
      const attached = e?.behaviors || [];
      Kit.openMenu([
        ...(attached.length ? [{ group: e.name }, ...attached.map((b) => ({ label: b, icon: 'graph', checked: b === beh, run: () => App.openDoc('behavior', b) })), '-'] : []),
        { label: 'All Behaviors', items: Object.keys(App.F.behaviors).map((b) => ({ label: b, icon: 'graph', checked: b === beh, run: () => App.openDoc('behavior', b) })) },
      ], ev.currentTarget, { anchor: ev.currentTarget });
    });
    $('.g-sel', c).addEventListener('pointerdown', (ev) => {
      ev.preventDefault();
      Kit.openMenu(behavior(beh).graphs.map((x, i) => ({ label: graphLabel(x), icon: x.kind === 'event' ? 'event' : 'fn', checked: i === App.State.graph.index, run: () => { App.State.graph.index = i; App.State.graph.selected = []; framed = null; App.refresh('graph'); } })), ev.currentTarget, { anchor: ev.currentTarget });
    });
  }

  function pinRow(n, p, dir) {
    const connected = current().edges.some((ed) => (dir === 'in' ? ed.to === n.id && ed.to_pin === p.id : ed.from === n.id && ed.from_pin === p.id));
    const socket = `<span class="socket ${p.kind === 'exec' ? 'exec' : ''}" data-pin="${p.id}" data-dir="${dir}" data-type="${p.type}" ${!connected && p.kind !== 'exec' ? 'data-hollow' : ''} style="background:${typeColor(p.kind === 'exec' ? 'exec' : p.type)};color:${typeColor(p.type)}"></span>`;
    if (dir === 'out') return `<div class="pin-row out">${p.name === 'Then' && n.outputs.filter((x) => x.kind === 'exec').length === 1 ? '' : `<span class="pin-label">${p.name}</span>`}${socket}</div>`;
    if (p.kind === 'exec') return `<div class="pin-row in">${socket}</div>`;
    let control = '';
    if (!connected) {
      if (p.binding) control = `<span class="binding" data-tip="${p.binding}">${icon('field')}<span>${p.binding.split('.').slice(0, 1)[0].replace(/^Game/, '')}.${p.binding.split('.').slice(1).join('.')}</span></span>`;
      else if (typeof p.default === 'number') control = `<span class="field-slot" data-pin="${p.id}"></span>`;
      else if (typeof p.default === 'boolean') control = `<span class="check-slot2" data-pin="${p.id}"></span>`;
      else if (typeof p.default === 'string') control = `<span class="binding" style="color:var(--fg)">${p.default}</span>`;
    }
    return `<div class="pin-row in">${socket}<span class="pin-label">${p.name}</span>${control}</div>`;
  }
  function nodeHtml(n) {
    const kind = KIND(n);
    const title = kind === 'Event' ? `On ${n.title}` : n.title;
    const execIn = n.inputs.find((p) => p.kind === 'exec'), execOuts = n.outputs.filter((p) => p.kind === 'exec');
    const dataIn = n.inputs.filter((p) => p.kind !== 'exec'), dataOut = n.outputs.filter((p) => p.kind !== 'exec' && !(kind === 'Expression' && p.name === 'Result' && execOuts.length > 1));
    const sel = App.State.graph.selected.includes(n.id), active = App.State.graph.selected[App.State.graph.selected.length - 1] === n.id;
    let rows = '';
    if (execIn || execOuts.length) rows += `<div class="pin-row pair">${execIn ? pinRow(n, execIn, 'in') : '<span></span>'}${execOuts.length ? pinRow(n, execOuts[0], 'out') : ''}</div>`;
    rows += execOuts.slice(1).map((p) => pinRow(n, p, 'out')).join('');
    return `<div class="node" data-id="${n.id}" data-kind="${kind}" ${sel ? 'data-selected' : ''} ${active ? 'data-active' : ''} style="left:${n.x}px;top:${n.y}px">
      <div class="node-head" ${kind === 'Expression' ? `data-tip="${n.title}"` : ''}>${icon(KIND_ICON[kind])}<span class="title ${kind === 'Expression' ? 'code' : ''}">${title}</span></div>
      <div class="node-body">${rows}${dataOut.map((p) => pinRow(n, p, 'out')).join('')}${dataIn.map((p) => pinRow(n, p, 'in')).join('')}</div>
    </div>`;
  }
  function render() {
    const g = current();
    nodesEl.innerHTML = g.nodes.map(nodeHtml).join('');
    for (const slot of $$('.field-slot', nodesEl)) {
      const nid = +slot.closest('.node').dataset.id, n = g.nodes.find((x) => x.id === nid), p = n.inputs.find((x) => x.id === +slot.dataset.pin);
      const f = h('<div></div>');
      slot.replaceWith(f);
      Kit.numberField(f, { value: p.default, step: p.type === 'int' ? 1 : 0.1, precision: p.type === 'int' ? 0 : 2, integer: p.type === 'int', onChange: (v) => { p.default = v; }, onCommit: (v, before) => { p.default = before; const nv = v; record(`${n.title} ${p.name}`, () => (p.default = nv), () => (p.default = before)); } });
    }
    for (const slot of $$('.check-slot2', nodesEl)) {
      const nid = +slot.closest('.node').dataset.id, n = g.nodes.find((x) => x.id === nid), p = n.inputs.find((x) => x.id === +slot.dataset.pin);
      const c = h('<span></span>');
      slot.replaceWith(c);
      Kit.checkbox(c, { value: () => p.default, onChange: (v) => record(`${n.title} ${p.name}`, () => (p.default = v), () => (p.default = !v)) });
    }
    applyView();
    requestAnimationFrame(wires);
  }
  function socketPos(nodeId, pinId) {
    const s = $(`.node[data-id="${nodeId}"] .socket[data-pin="${pinId}"]`, nodesEl);
    if (!s) return null;
    const n = s.closest('.node');
    let x = s.offsetLeft + s.offsetWidth / 2, y = s.offsetTop + s.offsetHeight / 2, el = s.offsetParent;
    while (el && el !== n) { x += el.offsetLeft; y += el.offsetTop; el = el.offsetParent; }
    return { x: n.offsetLeft + x, y: n.offsetTop + y };
  }
  const curve = (a, b) => { const dx = Math.max(40, Math.abs(b.x - a.x) * 0.5); return `M${a.x},${a.y} C${a.x + dx},${a.y} ${b.x - dx},${b.y} ${b.x},${b.y}`; };
  function wires() {
    const g = current();
    let html = '';
    for (const ed of g.edges) {
      const a = socketPos(ed.from, ed.from_pin), b = socketPos(ed.to, ed.to_pin);
      if (!a || !b) continue;
      const pin = g.nodes.find((n) => n.id === ed.from)?.outputs.find((p) => p.id === ed.from_pin);
      const exec = ed.kind === 'exec';
      const sel = App.State.graph.selected.includes(ed.from) || App.State.graph.selected.includes(ed.to);
      html += `<path d="${curve(a, b)}" stroke="${exec ? '#d8d8dc' : typeColor(pin?.type)}" stroke-width="${exec ? 2.2 : 1.6}" opacity="${sel ? 1 : 0.78}"/>`;
    }
    if (wire) html += `<path d="${wire.dir === 'out' ? curve(wire.a, wire.b) : curve(wire.b, wire.a)}" stroke="${wire.color}" stroke-width="2" stroke-dasharray="${wire.snap ? '' : '5 4'}"/>`;
    svg.innerHTML = html;
  }
  function applyView() {
    stage.style.transform = `translate(${view.x}px, ${view.y}px) scale(${view.zoom})`;
    $('.zoom-readout', root).textContent = `${Math.round(view.zoom * 100)}%`;
    drawGrid();
  }
  function drawGrid() {
    const dpr = devicePixelRatio, W = grid.clientWidth, H = grid.clientHeight;
    if (!W) return;
    if (grid.width !== W * dpr || grid.height !== H * dpr) { grid.width = W * dpr; grid.height = H * dpr; }
    const c = grid.getContext('2d');
    c.setTransform(dpr, 0, 0, dpr, 0, 0);
    c.fillStyle = '#1c1c1e'; c.fillRect(0, 0, W, H);
    const step = 24 * view.zoom;
    if (step < 6) return;
    c.fillStyle = '#ffffff16';
    const ox = ((view.x % step) + step) % step, oy = ((view.y % step) + step) % step;
    for (let x = ox; x < W; x += step) for (let y = oy; y < H; y += step) c.fillRect(Math.round(x), Math.round(y), 1.5, 1.5);
    c.fillStyle = '#ffffff22';
    const big = step * 5, bx = ((view.x % big) + big) % big, by = ((view.y % big) + big) % big;
    for (let x = bx; x < W; x += big) for (let y = by; y < H; y += big) c.fillRect(Math.round(x) - 0.5, Math.round(y) - 0.5, 2.5, 2.5);
  }
  function frame(which) {
    const g = current();
    const list = which === 'selected' && App.State.graph.selected.length ? g.nodes.filter((n) => App.State.graph.selected.includes(n.id)) : g.nodes;
    if (!list.length) return;
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (const n of list) { const el = $(`.node[data-id="${n.id}"]`, nodesEl); x0 = Math.min(x0, n.x); y0 = Math.min(y0, n.y); x1 = Math.max(x1, n.x + (el?.offsetWidth || 250)); y1 = Math.max(y1, n.y + (el?.offsetHeight || 120)); }
    const W = root.clientWidth, H = root.clientHeight;
    const fit = Math.min((W - 60) / (x1 - x0), (H - 60) / (y1 - y0));
    view.zoom = clamp(fit, which === 'all' ? 0.6 : 0.25, 1.1);
    view.x = fit < view.zoom ? 30 - x0 * view.zoom : (W - (x1 - x0) * view.zoom) / 2 - x0 * view.zoom;
    view.y = (H - (y1 - y0) * view.zoom) / 2 - y0 * view.zoom;
    applyView();
  }
  const local = (e) => { const r = root.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  const toGraph = (p) => ({ x: (p.x - view.x) / view.zoom, y: (p.y - view.y) / view.zoom });
  const selectNodes = (ids, mode) => {
    const s = App.State.graph;
    if (mode === 'toggle') s.selected = s.selected.includes(ids[0]) ? s.selected.filter((x) => x !== ids[0]) : [...s.selected, ids[0]];
    else s.selected = ids;
    App.State.focus = 'graph';
    App.refresh('graph-selection');
  };

  function addNode(spec, at, connectFrom) {
    const g = current();
    const n = { id: nextNodeId++, type: spec.type, title: spec.title, x: Math.round(at.x), y: Math.round(at.y), inputs: spec.inputs.map((p) => ({ ...p, id: nextNodeId++ })), outputs: spec.outputs.map((p) => ({ ...p, id: nextNodeId++ })) };
    let edge = null;
    if (connectFrom) {
      const target = n.inputs.find((p) => (connectFrom.kind === 'exec' ? p.kind === 'exec' : p.kind !== 'exec'));
      if (target) edge = { from: connectFrom.node, from_pin: connectFrom.pin, to: n.id, to_pin: target.id, kind: connectFrom.kind === 'exec' ? 'exec' : 'data', order: 0 };
    }
    record(`Add ${spec.title}`, () => { g.nodes.push(n); if (edge) g.edges.push(edge); App.State.graph.selected = [n.id]; }, () => { g.nodes.splice(g.nodes.indexOf(n), 1); if (edge) g.edges.splice(g.edges.indexOf(edge), 1); App.State.graph.selected = []; });
  }
  function addPalette(at, connectFrom) {
    const items = () => NODE_LIBRARY.filter((s) => !connectFrom || (connectFrom.kind === 'exec' ? s.inputs.some((p) => p.kind === 'exec') : s.inputs.some((p) => p.kind !== 'exec'))).map((s, i) => ({ id: `node${i}`, group: s.group, label: s.title, icon: KIND_ICON[KIND(s)], meta: s.type, action: 'Add', run: () => addNode(s, at, connectFrom) }));
    App.palette({ label: 'Add Node', items });
  }
  function deleteNodes(ids) {
    const g = current();
    const nodes = g.nodes.filter((n) => ids.includes(n.id));
    if (!nodes.length) return;
    const edges = g.edges.filter((e) => ids.includes(e.from) || ids.includes(e.to));
    record(nodes.length === 1 ? `Delete ${nodes[0].title}` : `Delete ${nodes.length} nodes`,
      () => { g.nodes = g.nodes.filter((n) => !nodes.includes(n)); g.edges = g.edges.filter((e) => !edges.includes(e)); App.State.graph.selected = []; },
      () => { g.nodes.push(...nodes); g.edges.push(...edges); App.State.graph.selected = ids; });
  }

  root.addEventListener('pointerdown', (e) => {
    const p = local(e);
    App.State.focus = 'graph';
    if (e.target.closest('.field, .check, .binding')) return;
    root.setPointerCapture(e.pointerId);
    if (e.button === 1 || (e.button === 0 && e.altKey)) { const v0 = { ...view }; drag = { move: (q) => { view.x = v0.x + q.x - p.x; view.y = v0.y + q.y - p.y; applyView(); } }; return; }
    if (e.button !== 0) return;
    const socket = e.target.closest('.socket');
    if (socket) {
      const nid = +socket.closest('.node').dataset.id, pid = +socket.dataset.pin, g = current();
      let from = { node: nid, pin: pid, kind: socket.classList.contains('exec') ? 'exec' : 'data', dir: socket.dataset.dir };
      let detached = null;
      if (from.dir === 'in') {
        detached = g.edges.find((ed) => ed.to === nid && ed.to_pin === pid);
        if (detached) { g.edges.splice(g.edges.indexOf(detached), 1); from = { node: detached.from, pin: detached.from_pin, kind: detached.kind, dir: 'out' }; }
      }
      const a = socketPos(from.node, from.pin);
      wire = { a, b: a, dir: from.dir, color: from.kind === 'exec' ? '#d8d8dc' : socket.style.color };
      if (detached) render();
      drag = {
        move: (q, ev) => {
          const t = document.elementFromPoint(ev.clientX, ev.clientY)?.closest('.socket');
          const ok = t && t.dataset.dir !== from.dir && t.classList.contains('exec') === (from.kind === 'exec');
          wire.snap = !!ok;
          wire.b = ok ? socketPos(+t.closest('.node').dataset.id, +t.dataset.pin) : toGraph(q);
          wires();
        },
        up: (q, ev) => {
          const t = document.elementFromPoint(ev.clientX, ev.clientY)?.closest('.socket');
          wire = null;
          if (t && t.dataset.dir !== from.dir && t.classList.contains('exec') === (from.kind === 'exec')) {
            const other = { node: +t.closest('.node').dataset.id, pin: +t.dataset.pin };
            const edge = from.dir === 'out' ? { from: from.node, from_pin: from.pin, to: other.node, to_pin: other.pin, kind: from.kind, order: 0 } : { from: other.node, from_pin: other.pin, to: from.node, to_pin: from.pin, kind: from.kind, order: 0 };
            const replaced = g.edges.filter((ed) => (from.kind === 'exec' ? ed.from === edge.from && ed.from_pin === edge.from_pin : ed.to === edge.to && ed.to_pin === edge.to_pin));
            if (detached) g.edges.push(detached);
            record('Connect', () => { if (detached) g.edges.splice(g.edges.indexOf(detached), 1); replaced.forEach((r) => g.edges.includes(r) && g.edges.splice(g.edges.indexOf(r), 1)); g.edges.push(edge); }, () => { g.edges.splice(g.edges.indexOf(edge), 1); g.edges.push(...replaced); if (detached && !g.edges.includes(detached)) g.edges.push(detached); });
            return;
          }
          if (detached) { g.edges.push(detached); record('Disconnect', () => g.edges.splice(g.edges.indexOf(detached), 1), () => g.edges.push(detached)); return; }
          if (from.dir === 'out') addPalette(toGraph(q), from);
          wires();
        },
      };
      return;
    }
    const nodeEl = e.target.closest('.node');
    if (nodeEl) {
      const id = +nodeEl.dataset.id;
      if (e.shiftKey) { selectNodes([id], 'toggle'); return; }
      if (!App.State.graph.selected.includes(id)) selectNodes([id]);
      else selectNodes([...App.State.graph.selected.filter((x) => x !== id), id]);
      const g = current();
      const moving = g.nodes.filter((n) => App.State.graph.selected.includes(n.id));
      const start = moving.map((n) => ({ x: n.x, y: n.y }));
      let moved = false;
      drag = {
        move: (q) => {
          const dx = (q.x - p.x) / view.zoom, dy = (q.y - p.y) / view.zoom;
          if (!moved && Math.hypot(dx, dy) < 3) return;
          moved = true;
          moving.forEach((n, i) => { n.x = Math.round(start[i].x + dx); n.y = Math.round(start[i].y + dy); const el = $(`.node[data-id="${n.id}"]`, nodesEl); el.style.left = `${n.x}px`; el.style.top = `${n.y}px`; });
          wires();
        },
        up: () => {
          if (!moved) return;
          const end = moving.map((n) => ({ x: n.x, y: n.y }));
          moving.forEach((n, i) => Object.assign(n, start[i]));
          record(moving.length === 1 ? `Move ${moving[0].title}` : `Move ${moving.length} nodes`, () => moving.forEach((n, i) => Object.assign(n, end[i])), () => moving.forEach((n, i) => Object.assign(n, start[i])));
        },
      };
      return;
    }
    marquee = h('<div class="marquee"></div>');
    root.appendChild(marquee);
    const base = e.shiftKey ? [...App.State.graph.selected] : [];
    drag = {
      move: (q) => { Object.assign(marquee.style, { left: `${Math.min(p.x, q.x)}px`, top: `${Math.min(p.y, q.y)}px`, width: `${Math.abs(q.x - p.x)}px`, height: `${Math.abs(q.y - p.y)}px` }); },
      up: (q) => {
        marquee.remove(); marquee = null;
        if (Math.hypot(q.x - p.x, q.y - p.y) < 3) { selectNodes(base); return; }
        const a = toGraph({ x: Math.min(p.x, q.x), y: Math.min(p.y, q.y) }), b = toGraph({ x: Math.max(p.x, q.x), y: Math.max(p.y, q.y) });
        const ids = current().nodes.filter((n) => { const el = $(`.node[data-id="${n.id}"]`, nodesEl); return n.x < b.x && n.x + el.offsetWidth > a.x && n.y < b.y && n.y + el.offsetHeight > a.y; }).map((n) => n.id);
        selectNodes([...new Set([...base, ...ids])]);
      },
    };
  });
  root.addEventListener('pointermove', (e) => { lastPointer = local(e); if (drag) drag.move(local(e), e); });
  root.addEventListener('pointerup', (e) => { if (drag) { const d = drag; drag = null; d.up?.(local(e), e); } });
  root.addEventListener('wheel', (e) => {
    if (e.target.closest('.field')) return;
    e.preventDefault();
    const p = local(e), before = toGraph(p);
    view.zoom = clamp(view.zoom * Math.exp(-e.deltaY * (e.ctrlKey ? 0.01 : 0.0015)), 0.2, 2);
    view.x = p.x - before.x * view.zoom; view.y = p.y - before.y * view.zoom;
    applyView();
  }, { passive: false });
  root.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    const nodeEl = e.target.closest('.node');
    const at = toGraph(local(e));
    if (nodeEl) {
      const id = +nodeEl.dataset.id;
      if (!App.State.graph.selected.includes(id)) selectNodes([id]);
      Kit.contextMenu(e, [
        { group: current().nodes.find((n) => n.id === id)?.title },
        { label: 'Frame', kbd: 'F', icon: 'frame', run: () => frame('selected') },
        { label: 'Toggle Breakpoint', kbd: 'F9', icon: 'dot', run: () => App.report('Breakpoint set') },
        { label: 'Disable', kbd: 'M', run: () => App.report('Node disabled') },
        '-',
        { label: 'Delete', kbd: 'X', icon: 'trash', run: () => deleteNodes(App.State.graph.selected) },
      ]);
      return;
    }
    Kit.contextMenu(e, [{ group: 'Add' }, ...['Flow', 'Math', 'Components', 'Entities', 'Events'].map((grp) => ({ label: grp, items: NODE_LIBRARY.filter((s) => s.group === grp).map((s) => ({ label: s.title, icon: KIND_ICON[KIND(s)], run: () => addNode(s, at) })) })), '-', { label: 'Search…', kbd: 'Shift+A', icon: 'search', run: () => addPalette(at) }, { label: 'Frame All', kbd: 'Home', run: () => frame('all') }]);
  });
  $$('.menu-btn', head).forEach((b) => b.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    const at = toGraph({ x: root.clientWidth / 2, y: root.clientHeight / 2 });
    const m = {
      view: [{ label: 'Frame Selected', kbd: 'F', icon: 'frame', run: () => frame('selected') }, { label: 'Frame All', kbd: 'Home', run: () => frame('all') }, '-', { label: 'Maximize Area', kbd: 'Ctrl+Space', icon: 'maximize', run: () => App.toggleMaximize(area) }],
      select: [{ label: 'All', kbd: 'A', run: () => selectNodes(current().nodes.map((n) => n.id)) }, { label: 'None', kbd: 'Alt+A', run: () => selectNodes([]) }],
      add: [...['Flow', 'Math', 'Components', 'Entities', 'Events'].map((grp) => ({ label: grp, items: NODE_LIBRARY.filter((s) => s.group === grp).map((s) => ({ label: s.title, icon: KIND_ICON[KIND(s)], run: () => addNode(s, at) })) })), '-', { label: 'Search…', kbd: 'Shift+A', icon: 'search', run: () => addPalette(at) }],
    }[b.dataset.m];
    Kit.openMenu(m, b, { anchor: b });
  }));
  $('.frame', head).addEventListener('click', () => frame('all'));
  $('.max', head).addEventListener('click', () => App.toggleMaximize(area));
  const ro = new ResizeObserver(() => { drawGrid(); if (!framed) { framed = true; frame('all'); } });
  ro.observe(root);
  App.graphSubject = () => { const g = current(); return { behavior: App.State.graph.behavior, graph: g, nodes: g.nodes.filter((n) => App.State.graph.selected.includes(n.id)) }; };
  let lastKey = '';
  return {
    hints: () => [['icon:mouse', 'Select'], ['icon:pan', 'Pan (middle)'], ['Shift+A', 'Add'], ['icon:branch', 'Drag socket to connect'], ['F', 'Frame'], ['X', 'Delete']],
    key(e) {
      const k = e.key.toLowerCase();
      if (e.shiftKey && k === 'a') { addPalette(toGraph(lastPointer)); return true; }
      if (e.metaKey || e.ctrlKey) return false;
      if (k === 'f') { frame('selected'); return true; }
      if (e.key === 'Home') { frame('all'); return true; }
      if (k === 'x' || e.key === 'Delete' || e.key === 'Backspace') { deleteNodes(App.State.graph.selected); return true; }
      if (k === 'a' && e.altKey) { selectNodes([]); return true; }
      if (k === 'a') { selectNodes(current().nodes.map((n) => n.id)); return true; }
      return false;
    },
    update(what) {
      if (['runtime-tick', 'layout', 'live', 'log', 'drawer'].includes(what)) { if (what === 'layout') drawGrid(); return; }
      const key = `${App.State.graph.behavior}:${App.State.graph.index}`;
      if (key !== lastKey) { lastKey = key; framed = null; }
      crumbs();
      render();
      if (!framed && root.clientWidth) { framed = true; requestAnimationFrame(() => frame('all')); }
    },
    destroy() { ro.disconnect(); },
  };
};
})();
