/* Scene viewport and Outliner editors. */
'use strict';
(() => {
const { $, $$, h, clamp } = Kit;
const rgba = (u, a = 1) => `rgba(${(u >>> 24) & 255},${(u >>> 16) & 255},${(u >>> 8) & 255},${a * ((u & 255) / 255)})`;
const SHAPES = ['Box', 'Circle', 'Diamond', 'Chevron', 'Stroked chevron', 'Capsule', 'Triangle', 'Stroked triangle'];

/* World geometry for drawing and hit testing. Screen Y follows world Y (matches the 2D presentation). */
function geometry(e, subject) {
  const s = subject || { kind: 'scene', entity: e };
  const proto = subject?.kind === 'prototype' ? App.prototypes[subject.name] : App.prototypes[e.prototype] || {};
  if (!proto.Transform && !subject) return null;
  const pos = subject?.kind === 'prototype' ? { x: 0, y: 0 } : App.position(e);
  const rot = App.read(s, 'Orientation', 'radians') || 0;
  if (proto.Camera) return { kind: 'camera', ...pos, w: App.read(s, 'Camera', 'width'), h: App.read(s, 'Camera', 'height'), rot: 0, color: 0x8a9daaff };
  if (proto.MaterialTerrain) return { kind: 'terrain', ...pos, w: App.read(s, 'MaterialTerrain', 'width'), h: App.read(s, 'MaterialTerrain', 'height'), rot: 0, color: App.read(s, 'Appearance', 'color') ?? 0x536268ff };
  if (proto.FluidVolume) return { kind: 'fluid', ...pos, w: App.read(s, 'FluidVolume', 'width'), h: App.read(s, 'FluidVolume', 'height'), rot: 0, color: App.read(s, 'FluidVolume', 'color') ?? 0x70909dff };
  if (proto.Appearance) return { kind: 'shape', shape: App.read(s, 'Appearance', 'shape'), ...pos, w: App.read(s, 'Appearance', 'width') || 10, h: App.read(s, 'Appearance', 'height') || 10, rot, color: App.read(s, 'Appearance', 'color') ?? 0xffffffff };
  return { kind: 'empty', ...pos, w: 16, h: 16, rot: 0, color: 0xaaaaaaff };
}

App.EDITORS.viewport = (area, head, body) => {
  const cam = { x: -120, y: 0, zoom: 0.9 };
  let hover = null, drag = null, modal = null, marquee = null;
  const img = new Image();
  img.src = 'assets/sandbox-render.png';
  img.onload = () => draw();

  head.insertAdjacentHTML('beforeend', `
    <button class="btn menu-btn" data-m="view">View</button><button class="btn menu-btn" data-m="select">Select</button><button class="btn menu-btn" data-m="add">Add</button>
    <span class="spacer"></span>
    <span class="shading"></span>
    <button class="btn icon-only overlays" data-tip="Overlays">${icon('overlay')}</button>
    <button class="btn icon-only snap" data-tip="Snap to grid" data-kbd="Shift+Tab">${icon('magnet')}</button>
    <button class="btn icon-only max" data-tip="Maximize area" data-kbd="Ctrl+Space">${icon('maximize')}</button>`);
  body.innerHTML = `<div class="viewport">
    <canvas class="scene"></canvas>
    <div class="toolshelf" role="toolbar">
      <button class="btn" data-tool="select" data-tip="Select box" data-kbd="W" data-tip-side="right">${icon('select')}</button>
      <button class="btn" data-tool="move" data-tip="Move" data-kbd="G" data-tip-side="right">${icon('move')}</button>
      <button class="btn" data-tool="rotate" data-tip="Rotate" data-kbd="R" data-tip-side="right">${icon('rotate')}</button>
      <button class="btn" data-tool="scale" data-tip="Scale" data-kbd="S" data-tip-side="right">${icon('scale')}</button>
      <div class="shelf-sep"></div>
      <button class="btn" data-act="frame" data-tip="Frame selected" data-kbd="F" data-tip-side="right">${icon('focus')}</button>
    </div>
    <div class="view-label"><span>Top orthographic</span><span class="dim ctx"></span></div>
    <div class="navgizmo">
      <svg class="axes" viewBox="-27 -27 54 54"><circle r="26" fill="#ffffff08"/><line x1="0" y1="0" x2="18" y2="0" stroke="#e0727a" stroke-width="2"/><line x1="0" y1="0" x2="0" y2="18" stroke="#82c46b" stroke-width="2"/><circle cx="18" cy="0" r="6" fill="#e0727a"/><circle cx="0" cy="18" r="6" fill="#82c46b"/><text x="18" y="3" font-size="8" text-anchor="middle" fill="#111" font-family="Inter" font-weight="700">X</text><text x="0" y="21" font-size="8" text-anchor="middle" fill="#111" font-family="Inter" font-weight="700">Y</text><circle cx="-18" cy="0" r="4" fill="#e0727a55"/><circle cx="0" cy="-18" r="4" fill="#82c46b55"/></svg>
      <button class="btn nav-zoom" data-tip="Drag to zoom">${icon('zoomIn')}</button>
      <button class="btn nav-pan" data-tip="Drag to pan">${icon('pan')}</button>
    </div>
    <span class="zoom-readout"></span>
    <span class="live-badge badge accent" hidden>Live</span>
    <div class="drop-hint"></div>
  </div>`;
  const vp = $('.viewport', body), canvas = $('canvas', vp), g = canvas.getContext('2d');
  let snap = false;

  const shading = Kit.toggleGroup($('.shading', head), {
    options: [{ value: 'solid', icon: 'circle', tip: 'Solid' }, { value: 'rendered', icon: 'light', tip: 'Rendered' }],
    value: () => App.State.shading, onChange: (v) => { App.State.shading = v; draw(); },
  });
  const menus = {
    view: () => [
      { label: 'Frame Selected', kbd: 'F', icon: 'frame', run: () => frame('selected') },
      { label: 'Frame All', kbd: 'Home', run: () => frame('all') },
      '-',
      { label: 'Zoom In', kbd: '=', icon: 'zoomIn', run: () => zoomAt(1.25) },
      { label: 'Zoom Out', kbd: '-', icon: 'zoomOut', run: () => zoomAt(0.8) },
      { label: 'Zoom 1:1', kbd: '1', run: () => { cam.zoom = 1; draw(); } },
      '-',
      { label: 'Maximize Area', kbd: 'Ctrl+Space', icon: 'maximize', run: () => App.toggleMaximize(area) },
    ],
    select: () => [
      { label: 'All', kbd: 'A', run: () => App.select(visible().map((e) => e.id)) },
      { label: 'None', kbd: 'Alt+A', run: () => App.select([]) },
      { label: 'Invert', kbd: 'Mod+I', run: () => App.select(visible().filter((e) => !App.State.selection.includes(e.id)).map((e) => e.id)) },
      '-',
      { label: 'Same Prototype', kbd: 'Shift+G', disabled: !App.activeEntity(), run: () => App.select(App.entities.filter((e) => e.prototype === App.activeEntity().prototype).map((e) => e.id)) },
    ],
    add: () => addMenu(worldAt(canvas.width / 2 / devicePixelRatio, canvas.height / 2 / devicePixelRatio)),
  };
  $$('.menu-btn', head).forEach((b) => b.addEventListener('pointerdown', (e) => { e.preventDefault(); Kit.openMenu(menus[b.dataset.m](), b, { anchor: b }); }));
  $('.overlays', head).addEventListener('pointerdown', (e) => {
    e.preventDefault();
    const o = App.State.overlays;
    Kit.openMenu([{ group: 'Overlays' },
      { label: 'Grid', checked: o.grid, run: () => { o.grid = !o.grid; draw(); } },
      { label: 'Labels', checked: o.labels, run: () => { o.labels = !o.labels; draw(); } },
      { label: 'Cameras and volumes', checked: o.helpers, run: () => { o.helpers = !o.helpers; draw(); } }], e.currentTarget, { anchor: e.currentTarget, place: { side: 'bottom', align: 'end', offset: 2 } });
  });
  $('.snap', head).addEventListener('click', (e) => { snap = !snap; if (snap) e.currentTarget.dataset.state = 'on'; else delete e.currentTarget.dataset.state; });
  $('.max', head).addEventListener('click', () => App.toggleMaximize(area));
  $$('.toolshelf [data-tool]', vp).forEach((b) => b.addEventListener('click', () => { App.State.tool = b.dataset.tool; renderTools(); }));
  $('.toolshelf [data-act=frame]', vp).addEventListener('click', () => frame('selected'));
  const renderTools = () => { $$('.toolshelf [data-tool]', vp).forEach((b) => { if (b.dataset.tool === App.State.tool) b.dataset.state = 'on'; else delete b.dataset.state; }); vp.dataset.tool = App.State.tool; };

  function subjectDoc() { const d = App.doc(App.State.doc); return d.kind === 'prototype' ? { kind: 'prototype', name: d.name } : null; }
  const visible = () => (subjectDoc() ? [] : App.entities.filter((e) => !e.hidden));
  function items() {
    const sd = subjectDoc();
    if (sd) return [{ e: { id: -1, name: sd.name, prototype: sd.name }, g: geometry(null, sd) }];
    return visible().map((e) => ({ e, g: geometry(e) })).filter((x) => x.g);
  }
  const toScreen = (x, y) => ({ x: (x - cam.x) * cam.zoom + canvas.clientWidth / 2, y: (y - cam.y) * cam.zoom + canvas.clientHeight / 2 });
  const worldAt = (sx, sy) => ({ x: (sx - canvas.clientWidth / 2) / cam.zoom + cam.x, y: (sy - canvas.clientHeight / 2) / cam.zoom + cam.y });

  function draw() {
    const dpr = devicePixelRatio, W = canvas.clientWidth, H = canvas.clientHeight;
    if (!W || !H) return;
    if (canvas.width !== W * dpr || canvas.height !== H * dpr) { canvas.width = W * dpr; canvas.height = H * dpr; }
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = '#1e1e20'; g.fillRect(0, 0, W, H);
    const rendered = App.State.shading === 'rendered';
    if (rendered && img.complete && !subjectDoc()) {
      const a = toScreen(-800, -270), b = toScreen(160, 270);
      g.fillStyle = '#050607'; g.fillRect(0, 0, W, H);
      g.drawImage(img, a.x, a.y, b.x - a.x, b.y - a.y);
    }
    if (App.State.overlays.grid) drawGrid(W, H, rendered);
    const sel = App.State.selection, active = sel[sel.length - 1];
    const list = items().sort((p, q) => (p.g.kind === 'terrain' ? -1 : 0) - (q.g.kind === 'terrain' ? -1 : 0));
    for (const { e, g: geo } of list) {
      const isSel = subjectDoc() ? true : sel.includes(e.id), isActive = subjectDoc() ? true : e.id === active;
      if (!rendered || isSel || hover === e) drawEntity(geo, { rendered, isSel, isActive, hover: hover === e });
      if (App.State.overlays.labels && (isSel || hover === e)) {
        const p = toScreen(geo.x, geo.y + (geo.kind === 'shape' ? geo.h / 2 : 0));
        g.font = '500 11px Inter'; g.textAlign = 'center'; g.fillStyle = isActive ? '#ffd9a8' : '#d8d8dc';
        g.fillText(e.name, p.x, p.y + (geo.kind === 'shape' ? 16 : 4));
      }
    }
    if (marquee) {
      g.strokeStyle = '#ffffffaa'; g.setLineDash([4, 3]); g.lineWidth = 1;
      g.strokeRect(marquee.x0 + 0.5, marquee.y0 + 0.5, marquee.x1 - marquee.x0, marquee.y1 - marquee.y0);
      g.setLineDash([]); g.fillStyle = '#ffffff0c'; g.fillRect(marquee.x0, marquee.y0, marquee.x1 - marquee.x0, marquee.y1 - marquee.y0);
    }
    $('.zoom-readout', vp).textContent = `${Math.round(cam.zoom * 100)}%`;
    const d = App.doc(App.State.doc), ae = App.activeEntity();
    $('.ctx', vp).textContent = d.kind === 'prototype' ? `(Prototype) ${d.name}` : `(Sandbox) ${ae ? ae.name : '—'}`;
    $('.live-badge', vp).hidden = !App.State.runtime;
  }
  function drawGrid(W, H, rendered) {
    const unit = 32, px = unit * cam.zoom;
    const lines = (step, color) => {
      if (step * cam.zoom < 7) return;
      const w0 = worldAt(0, 0), w1 = worldAt(W, H);
      g.strokeStyle = color; g.lineWidth = 1; g.beginPath();
      for (let x = Math.floor(w0.x / step) * step; x <= w1.x; x += step) { const s = toScreen(x, 0).x; g.moveTo(Math.round(s) + 0.5, 0); g.lineTo(Math.round(s) + 0.5, H); }
      for (let y = Math.floor(w0.y / step) * step; y <= w1.y; y += step) { const s = toScreen(0, y).y; g.moveTo(0, Math.round(s) + 0.5); g.lineTo(W, Math.round(s) + 0.5); }
      g.stroke();
    };
    lines(unit, rendered ? '#ffffff08' : '#ffffff0a');
    lines(unit * 10, rendered ? '#ffffff10' : '#ffffff14');
    const o = toScreen(0, 0);
    g.lineWidth = 1.5;
    g.strokeStyle = '#e0727a99'; g.beginPath(); g.moveTo(0, Math.round(o.y) + 0.5); g.lineTo(W, Math.round(o.y) + 0.5); g.stroke();
    g.strokeStyle = '#82c46b99'; g.beginPath(); g.moveTo(Math.round(o.x) + 0.5, 0); g.lineTo(Math.round(o.x) + 0.5, H); g.stroke();
    void px;
  }
  function shapePath(geo) {
    const w = geo.w * cam.zoom, hh = geo.h * cam.zoom;
    g.beginPath();
    switch (geo.shape) {
      case 1: g.ellipse(0, 0, w / 2, hh / 2, 0, 0, Math.PI * 2); break;
      case 2: g.moveTo(0, -hh / 2); g.lineTo(w / 2, 0); g.lineTo(0, hh / 2); g.lineTo(-w / 2, 0); g.closePath(); break;
      case 3: case 4: g.moveTo(w / 2, 0); g.lineTo(-w / 2, -hh / 2); g.lineTo(-w / 4, 0); g.lineTo(-w / 2, hh / 2); g.closePath(); break;
      case 6: case 7: g.moveTo(w / 2, 0); g.lineTo(-w / 2, -hh / 2); g.lineTo(-w / 2, hh / 2); g.closePath(); break;
      default: g.roundRect(-w / 2, -hh / 2, w, hh, Math.min(3, w / 4));
    }
  }
  function drawEntity(geo, st) {
    const p = toScreen(geo.x, geo.y);
    const outline = st.isActive ? '#ffa53a' : st.isSel ? '#e0701c' : st.hover ? '#ffffff88' : null;
    g.save(); g.translate(p.x, p.y);
    if (geo.kind === 'shape') {
      g.rotate(geo.rot);
      shapePath(geo);
      if (!st.rendered) {
        const stroked = geo.shape === 4 || geo.shape === 7;
        g.fillStyle = stroked ? '#00000000' : rgba(geo.color, 0.22); g.fill();
        g.lineWidth = stroked ? 2 : 1.2; g.strokeStyle = rgba(geo.color, 0.75); g.stroke();
      }
      if (outline) { g.lineWidth = 2; g.strokeStyle = outline; shapePath({ ...geo, w: geo.w + 6 / cam.zoom, h: geo.h + 6 / cam.zoom }); g.stroke(); }
    } else {
      const w = geo.w * cam.zoom, hh = geo.h * cam.zoom;
      if (geo.kind === 'camera') {
        if (App.State.overlays.helpers || st.isSel) { g.setLineDash([6, 4]); g.lineWidth = 1; g.strokeStyle = outline || '#8a9daa88'; g.strokeRect(-w / 2, -hh / 2, w, hh); g.setLineDash([]); g.fillStyle = outline || '#8a9daa88'; g.beginPath(); g.moveTo(-8, -hh / 2 - 4); g.lineTo(8, -hh / 2 - 4); g.lineTo(0, -hh / 2 - 12); g.closePath(); g.fill(); }
      } else if (geo.kind === 'terrain') {
        if (!st.rendered) { g.fillStyle = '#ffffff05'; g.fillRect(-w / 2, -hh / 2, w, hh); }
        if (App.State.overlays.helpers || st.isSel) { g.lineWidth = 1; g.strokeStyle = outline || '#53626888'; g.strokeRect(-w / 2, -hh / 2, w, hh); }
      } else if (geo.kind === 'fluid') {
        if (!st.rendered) { g.fillStyle = rgba(geo.color | 0xff, 0.05); g.beginPath(); g.roundRect(-w / 2, -hh / 2, w, hh, Math.min(w, hh) * 0.3); g.fill(); }
        if (App.State.overlays.helpers || outline) { g.lineWidth = outline ? 2 : 1; g.setLineDash(outline ? [] : [3, 4]); g.strokeStyle = outline || rgba(geo.color | 0xff, 0.35); g.beginPath(); g.roundRect(-w / 2, -hh / 2, w, hh, Math.min(w, hh) * 0.3); g.stroke(); g.setLineDash([]); }
      } else {
        g.strokeStyle = outline || '#aaaaaa'; g.lineWidth = 1.2; g.beginPath(); g.moveTo(-6, 0); g.lineTo(6, 0); g.moveTo(0, -6); g.lineTo(0, 6); g.stroke();
      }
    }
    g.restore();
  }
  function hitTest(sx, sy) {
    const w = worldAt(sx, sy);
    const list = items().filter(({ g: geo }) => {
      if (geo.kind === 'terrain') return false;
      const pad = 4 / cam.zoom;
      if (geo.kind === 'camera') { const inside = Math.abs(w.x - geo.x) <= geo.w / 2 + pad && Math.abs(w.y - geo.y) <= geo.h / 2 + pad; const edge = inside && (Math.abs(Math.abs(w.x - geo.x) - geo.w / 2) < 8 / cam.zoom || Math.abs(Math.abs(w.y - geo.y) - geo.h / 2) < 8 / cam.zoom); return edge; }
      return Math.abs(w.x - geo.x) <= geo.w / 2 + pad && Math.abs(w.y - geo.y) <= geo.h / 2 + pad;
    });
    list.sort((a, b) => a.g.w * a.g.h - b.g.w * b.g.h);
    return list[0]?.e || null;
  }
  function frame(which) {
    const list = which === 'selected' && !subjectDoc() ? items().filter(({ e }) => App.State.selection.includes(e.id)) : items().filter(({ g: geo }) => geo.kind !== 'terrain' && geo.kind !== 'camera');
    if (!list.length) return;
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (const { g: geo } of list) { x0 = Math.min(x0, geo.x - geo.w / 2); x1 = Math.max(x1, geo.x + geo.w / 2); y0 = Math.min(y0, geo.y - geo.h / 2); y1 = Math.max(y1, geo.y + geo.h / 2); }
    cam.x = (x0 + x1) / 2; cam.y = (y0 + y1) / 2;
    const w = Math.max(x1 - x0, 240), hh = Math.max(y1 - y0, 160);
    cam.zoom = clamp(Math.min(canvas.clientWidth / (w * 1.4), canvas.clientHeight / (hh * 1.4)), 0.05, 4);
    draw();
  }
  function zoomAt(f, sx = canvas.clientWidth / 2, sy = canvas.clientHeight / 2) {
    const before = worldAt(sx, sy);
    cam.zoom = clamp(cam.zoom * f, 0.05, 8);
    const after = worldAt(sx, sy);
    cam.x += before.x - after.x; cam.y += before.y - after.y;
    draw();
  }
  function addMenu(at) {
    return Object.keys(App.prototypes).filter((p) => App.prototypes[p].Transform).map((p) => ({ label: p, icon: App.protoIcon(p), run: () => App.addEntity(p, at) }));
  }
  const local = (e) => { const r = canvas.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  const locked = () => !!App.State.runtime || !!subjectDoc();

  /* Modal transform (Blender G / R): follows the pointer, X/Y constrain, click confirms, Esc/RMB cancel. */
  function beginModal(kind, e0) {
    const sel = App.selected().filter((e) => !e.locked);
    if (!sel.length || locked()) return;
    const start = sel.map((e) => App.position(e));
    const rot0 = sel.map((e) => App.read({ kind: 'scene', entity: e }, 'Orientation', 'radians') || 0);
    const origin = lastPointer;
    const c = start.reduce((a, p) => ({ x: a.x + p.x / start.length, y: a.y + p.y / start.length }), { x: 0, y: 0 });
    modal = { kind, sel, start, rot0, origin, axis: null, c };
    const apply = (pt) => {
      const w0 = worldAt(origin.x, origin.y), w1 = worldAt(pt.x, pt.y);
      if (kind === 'move') {
        let dx = w1.x - w0.x, dy = w1.y - w0.y;
        if (modal.axis === 'x') dy = 0; if (modal.axis === 'y') dx = 0;
        if (snap || pt.ctrl) { dx = Math.round(dx / 32) * 32; dy = Math.round(dy / 32) * 32; }
        sel.forEach((e, i) => { e.overrides.Transform ??= {}; e.overrides.Transform.x = Math.round(start[i].x + dx); e.overrides.Transform.y = Math.round(start[i].y + dy); });
        App.setHints([['icon:mouse', 'Confirm'], ['Esc', 'Cancel'], ['X', 'Along X'], ['Y', 'Along Y'], ['Ctrl', 'Snap'], ['icon:move', `Δ ${Math.round(dx)}, ${Math.round(dy)}`]]);
      } else {
        const a0 = Math.atan2(w0.y - c.y, w0.x - c.x), a1 = Math.atan2(w1.y - c.y, w1.x - c.x);
        let d = a1 - a0;
        if (snap || pt.ctrl) d = Math.round(d / (Math.PI / 12)) * (Math.PI / 12);
        sel.forEach((e, i) => { if (App.prototypes[e.prototype]?.Orientation) { e.overrides.Orientation ??= {}; e.overrides.Orientation.radians = +(rot0[i] + d).toFixed(4); } });
        App.setHints([['icon:mouse', 'Confirm'], ['Esc', 'Cancel'], ['Ctrl', 'Snap 15°'], ['icon:rotate', `${Math.round(d * 180 / Math.PI)}°`]]);
      }
      App.refresh('values');
    };
    modal.apply = apply;
    modal.end = (ok) => {
      const m = modal; modal = null;
      if (!m) return;
      if (kind === 'move') {
        const after = sel.map((e) => ({ ...e.overrides.Transform }));
        sel.forEach((e, i) => Object.assign(e.overrides.Transform, start[i]));
        if (ok && after.some((p, i) => p.x !== start[i].x || p.y !== start[i].y)) App.moveEntities(sel, start, after, `Move ${sel.length === 1 ? sel[0].name : `${sel.length} objects`}`);
      } else {
        const after = sel.map((e) => e.overrides.Orientation?.radians);
        sel.forEach((e, i) => { if (e.overrides.Orientation) e.overrides.Orientation.radians = rot0[i]; });
        if (ok) sel.forEach((e, i) => after[i] !== undefined && after[i] !== rot0[i] && App.setValue({ kind: 'scene', entity: e }, 'Orientation', 'radians', after[i], rot0[i], `Rotate ${e.name}`));
      }
      App.setHints(hints());
      App.refresh('values');
    };
    if (e0) apply(e0);
  }
  let lastPointer = { x: 0, y: 0 };

  canvas.addEventListener('pointermove', (e) => {
    const p = local(e);
    lastPointer = p;
    if (modal) { modal.apply({ ...p, ctrl: e.ctrlKey || e.metaKey }); return; }
    if (drag) { drag.move(p, e); return; }
    const hit = hitTest(p.x, p.y);
    if (hit !== hover) { hover = hit; draw(); }
  });
  canvas.addEventListener('pointerleave', () => { if (hover) { hover = null; draw(); } });
  canvas.addEventListener('pointerdown', (e) => {
    const p = local(e);
    canvas.setPointerCapture(e.pointerId);
    if (modal) { modal.end(e.button === 0); return; }
    if (e.button === 1 || (e.button === 0 && e.altKey)) { const c0 = { ...cam }; drag = { move: (q) => { cam.x = c0.x - (q.x - p.x) / cam.zoom; cam.y = c0.y - (q.y - p.y) / cam.zoom; draw(); }, up: () => {} }; return; }
    if (e.button !== 0) return;
    const hit = subjectDoc() ? null : hitTest(p.x, p.y);
    const tool = App.State.tool;
    if ((tool === 'move' || tool === 'rotate') && !locked() && (hit || App.selected().length)) {
      if (hit && !App.State.selection.includes(hit.id)) App.select([hit.id]);
      lastPointer = p; beginModal(tool, null);
      drag = { move: (q, ev) => modal?.apply({ ...q, ctrl: ev.ctrlKey || ev.metaKey }), up: () => modal?.end(true) };
      return;
    }
    if (hit) {
      if (e.shiftKey) { App.select([hit.id], 'toggle'); return; }
      if (!App.State.selection.includes(hit.id)) App.select([hit.id]);
      else App.select([...App.State.selection.filter((x) => x !== hit.id), hit.id]);
      let started = false;
      drag = {
        move: (q, ev) => {
          if (!started && Math.hypot(q.x - p.x, q.y - p.y) < 4) return;
          if (!started) { started = true; if (locked()) { drag = null; return; } lastPointer = p; beginModal('move', null); }
          modal?.apply({ ...q, ctrl: ev.ctrlKey || ev.metaKey });
        },
        up: () => modal?.end(true),
      };
      return;
    }
    if (subjectDoc()) return;
    marquee = { x0: p.x, y0: p.y, x1: p.x, y1: p.y };
    const base = e.shiftKey ? [...App.State.selection] : [];
    drag = {
      move: (q) => { marquee.x1 = q.x; marquee.y1 = q.y; draw(); },
      up: () => {
        const m = marquee; marquee = null;
        if (Math.abs(m.x1 - m.x0) < 3 && Math.abs(m.y1 - m.y0) < 3) { App.select(base); return; }
        const a = worldAt(Math.min(m.x0, m.x1), Math.min(m.y0, m.y1)), b = worldAt(Math.max(m.x0, m.x1), Math.max(m.y0, m.y1));
        const ids = items().filter(({ g: geo }) => geo.kind !== 'terrain' && geo.x >= a.x && geo.x <= b.x && geo.y >= a.y && geo.y <= b.y).map(({ e: x }) => x.id);
        App.select([...new Set([...base, ...ids])]);
      },
    };
  });
  canvas.addEventListener('pointerup', () => { if (drag) { const d = drag; drag = null; d.up(); draw(); } });
  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    const p = local(e);
    if (e.shiftKey && !e.ctrlKey) { cam.x += e.deltaY / cam.zoom; draw(); return; }
    zoomAt(Math.exp(-e.deltaY * (e.ctrlKey ? 0.01 : 0.0015)), p.x, p.y);
  }, { passive: false });
  canvas.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    if (modal) { modal.end(false); return; }
    const p = local(e), hit = subjectDoc() ? null : hitTest(p.x, p.y);
    if (hit && !App.State.selection.includes(hit.id)) App.select([hit.id]);
    const sel = App.selected();
    Kit.contextMenu(e, sel.length ? [
      { group: sel.length === 1 ? sel[0].name : `${sel.length} objects` },
      { label: 'Move', kbd: 'G', icon: 'move', disabled: locked(), run: () => beginModal('move') },
      { label: 'Rotate', kbd: 'R', icon: 'rotate', disabled: locked(), run: () => beginModal('rotate') },
      '-',
      { label: 'Duplicate', kbd: 'Shift+D', icon: 'copy', disabled: locked(), run: () => App.duplicateEntities(sel) },
      { label: 'Open Prototype', icon: 'prototype', disabled: sel.length !== 1, run: () => App.openDoc('prototype', sel[0].prototype) },
      { label: 'Attach Behavior', icon: 'graph', disabled: sel.length !== 1, run: () => App.palette('behaviors') },
      '-',
      { label: 'Frame Selected', kbd: 'F', icon: 'frame', run: () => frame('selected') },
      { label: 'Hide', kbd: 'H', icon: 'eyeOff', run: () => { sel.forEach((x) => (x.hidden = true)); App.select([]); } },
      { label: 'Delete', kbd: 'X', icon: 'trash', disabled: locked(), run: () => App.deleteEntities(sel) },
    ] : [{ group: 'Add' }, ...addMenu(worldAt(p.x, p.y))]);
  });
  // Assets drag-and-drop (prototype → new instance at the drop point).
  vp.addEventListener('dragover', (e) => { if (e.dataTransfer.types.includes('chibi/prototype') && !locked()) { e.preventDefault(); vp.dataset.dropping = ''; } });
  vp.addEventListener('dragleave', () => delete vp.dataset.dropping);
  vp.addEventListener('drop', (e) => {
    delete vp.dataset.dropping;
    const name = e.dataTransfer.getData('chibi/prototype');
    if (!name) return;
    e.preventDefault();
    const p = local(e);
    App.addEntity(name, worldAt(p.x, p.y));
  });
  // Navigation gizmo buttons: drag to zoom / pan (Blender).
  const gizmoDrag = (btn, fn) => btn.addEventListener('pointerdown', (e) => {
    e.preventDefault(); btn.setPointerCapture(e.pointerId);
    let last = { x: e.clientX, y: e.clientY };
    const move = (ev) => { fn(ev.clientX - last.x, ev.clientY - last.y); last = { x: ev.clientX, y: ev.clientY }; };
    const up = () => { btn.removeEventListener('pointermove', move); btn.removeEventListener('pointerup', up); };
    btn.addEventListener('pointermove', move); btn.addEventListener('pointerup', up);
  });
  gizmoDrag($('.nav-zoom', vp), (dx, dy) => zoomAt(Math.exp(-dy * 0.01)));
  gizmoDrag($('.nav-pan', vp), (dx, dy) => { cam.x -= dx / cam.zoom; cam.y -= dy / cam.zoom; draw(); });

  const ro = new ResizeObserver(() => draw());
  ro.observe(vp);
  const hints = () => [['icon:mouse', 'Select'], ['Shift', 'Extend'], ['icon:pan', 'Pan (middle)'], ['G', 'Move'], ['R', 'Rotate'], ['Shift+A', 'Add'], ['F', 'Frame']];
  App.frame = (w) => (area.el.isConnected ? frame(w) : null);
  App.viewCenter = () => ({ x: cam.x, y: cam.y });
  let first = true;
  return {
    hints,
    key(e) {
      const k = e.key.toLowerCase();
      if (modal) {
        if (e.key === 'Escape') { modal.end(false); return true; }
        if (e.key === 'Enter') { modal.end(true); return true; }
        if (k === 'x' || k === 'y') { modal.axis = modal.axis === k ? null : k; modal.apply(lastPointer); return true; }
        return true;
      }
      if (e.shiftKey && k === 'a') { Kit.openMenu([{ group: 'Add' }, ...addMenu(worldAt(lastPointer.x, lastPointer.y))], { x: lastPointer.x + canvas.getBoundingClientRect().left, y: lastPointer.y + canvas.getBoundingClientRect().top }, { place: { side: 'bottom', align: 'start', offset: 0 }, keyboard: true }); return true; }
      if (e.shiftKey && k === 'd') { if (!locked() && App.selected().length) { App.duplicateEntities(App.selected()); beginModal('move'); } return true; }
      if (e.metaKey || e.ctrlKey) return false;
      if (k === 'g') { beginModal('move'); return true; }
      if (k === 'r') { beginModal('rotate'); return true; }
      if (k === 'f' || e.key === '.') { frame('selected'); return true; }
      if (e.key === 'Home') { frame('all'); return true; }
      if (k === 'x') { if (App.selected().length && !locked()) Kit.openMenu([{ label: 'Delete', icon: 'trash', run: () => App.deleteEntities(App.selected()) }], { x: lastPointer.x + canvas.getBoundingClientRect().left, y: lastPointer.y + canvas.getBoundingClientRect().top }, { keyboard: true }); return true; }
      if (e.key === 'Delete' || e.key === 'Backspace') { if (!locked()) App.deleteEntities(App.selected()); return true; }
      if (k === 'a' && e.altKey) { App.select([]); return true; }
      if (k === 'a') { App.select(visible().map((x) => x.id)); return true; }
      if (k === 'h' && e.altKey) { App.entities.forEach((x) => (x.hidden = false)); App.refresh('all'); return true; }
      if (k === 'h') { App.selected().forEach((x) => (x.hidden = true)); App.select([]); return true; }
      if (k === 'w') { App.State.tool = 'select'; renderTools(); return true; }
      if (e.key === '=' || e.key === '+') { zoomAt(1.25); return true; }
      if (e.key === '-') { zoomAt(0.8); return true; }
      return false;
    },
    update(what) {
      renderTools();
      shading.refresh();
      if (first && canvas.clientWidth) { first = false; cam.x = -180; cam.y = 0; cam.zoom = clamp(canvas.clientWidth / 1500, 0.3, 2); }
      if (what === 'all' && subjectDoc()) { requestAnimationFrame(() => frame('all')); }
      draw();
    },
    destroy() { ro.disconnect(); },
  };
};

/* ---------- Outliner ---------- */
App.EDITORS.outliner = (area, head, body) => {
  head.insertAdjacentHTML('beforeend', '<div class="field search" style="flex:1"></div><button class="btn icon-only filter" data-tip="Filter">' + icon('filter') + '</button>');
  const search = $('.search', head);
  search.innerHTML = `<span class="part addon">${icon('search', 'sm')}</span><input class="part value" placeholder="Filter" spellcheck="false"><span class="part clear" hidden>${icon('close', 'sm')}</span>`;
  const input = $('input', search);
  const opts = { systems: true, byPrototype: false };
  input.addEventListener('input', () => { App.State.outlinerFilter = input.value; render(); });
  input.addEventListener('keydown', (e) => { e.stopPropagation(); if (e.key === 'Escape') { input.value = ''; App.State.outlinerFilter = ''; render(); input.blur(); } if (e.key === 'ArrowDown') { e.preventDefault(); $('.tree-row[data-id]', body)?.focus(); } });
  $('.clear', search).addEventListener('click', () => { input.value = ''; App.State.outlinerFilter = ''; render(); });
  $('.filter', head).addEventListener('pointerdown', (e) => {
    e.preventDefault();
    Kit.openMenu([{ group: 'Filter' }, { label: 'Systems', checked: opts.systems, run: () => { opts.systems = !opts.systems; render(); } }, { label: 'Group by prototype', checked: opts.byPrototype, run: () => { opts.byPrototype = !opts.byPrototype; render(); } }], e.currentTarget, { anchor: e.currentTarget, place: { side: 'bottom', align: 'end', offset: 2 } });
  });
  body.innerHTML = '<div class="outliner"><div class="tree scroll" role="tree"></div></div>';
  const tree = $('.tree', body);
  const collapsed = new Set();
  let renaming = null, anchor = null;

  const isSystem = (e) => !App.prototypes[e.prototype]?.Transform;
  function hl(name, q) {
    if (!q) return name;
    const i = name.toLowerCase().indexOf(q.toLowerCase());
    return i < 0 ? name : `${name.slice(0, i)}<span class="hl">${name.slice(i, i + q.length)}</span>${name.slice(i + q.length)}`;
  }
  function row(e, depth) {
    const sel = App.State.selection.includes(e.id), active = App.activeEntity() === e;
    const q = App.State.outlinerFilter;
    return `<div class="tree-row" role="treeitem" tabindex="-1" data-id="${e.id}" ${sel ? 'data-selected' : ''} ${active ? 'data-active' : ''} style="padding-left:${6 + depth * 14}px;${e.hidden ? 'opacity:.5' : ''}">
      <span class="twisty"></span>${icon(App.protoIcon(e.prototype))}
      ${renaming === e.id ? `<div class="field text rename"><input class="part value" value="${e.name}" spellcheck="false"></div>` : `<span class="name">${hl(e.name, q)}</span>`}
      <span class="inline-icons">${e.behaviors.length ? `<span data-tip="${e.behaviors.join(', ')}">${icon('graph')}</span>${e.behaviors.length > 1 ? `<span class="meta">${e.behaviors.length}</span>` : ''}` : ''}</span>
      <button class="toggle vis" ${e.hidden ? 'data-off' : ''} data-tip="${e.hidden ? 'Show' : 'Hide'}" data-kbd="H">${icon(e.hidden ? 'eyeOff' : 'eye')}</button>
      <button class="toggle lock" ${e.locked ? '' : 'data-off'} data-tip="${e.locked ? 'Unlock' : 'Lock'}">${icon(e.locked ? 'lock' : 'unlock')}</button>
    </div>`;
  }
  function render() {
    const d = App.doc(App.State.doc);
    const q = App.State.outlinerFilter.trim().toLowerCase();
    if (d.kind === 'prototype') {
      const comps = Object.keys(App.prototypes[d.name]);
      tree.innerHTML = `<div class="tree-row" data-active style="padding-left:6px"><span class="twisty">${icon('down', 'sm')}</span>${icon(App.protoIcon(d.name))}<span class="name">${d.name}</span><span class="meta">Prototype</span></div>`
        + comps.map((c) => `<div class="tree-row" style="padding-left:34px">${icon('field')}<span class="name">${App.human(c)}</span></div>`).join('');
      return;
    }
    const match = (e) => !q || e.name.toLowerCase().includes(q) || e.prototype.toLowerCase().includes(q);
    const list = App.entities.filter((e) => match(e) && (opts.systems || !isSystem(e)));
    let html = `<div class="tree-row root" data-root style="padding-left:6px"><span class="twisty">${icon(collapsed.has('root') ? 'right' : 'down', 'sm')}</span>${icon('scene')}<span class="name">Sandbox</span><span class="meta num">${list.length}</span></div>`;
    if (!collapsed.has('root')) {
      if (opts.byPrototype) {
        const groups = {};
        list.forEach((e) => (groups[e.prototype] ??= []).push(e));
        for (const [p, es] of Object.entries(groups)) {
          html += `<div class="tree-row" data-group="${p}" style="padding-left:20px"><span class="twisty">${icon(collapsed.has(p) ? 'right' : 'down', 'sm')}</span>${icon('prototype')}<span class="name">${p}</span><span class="meta num">${es.length}</span></div>`;
          if (!collapsed.has(p)) html += es.map((e) => row(e, 2)).join('');
        }
      } else html += list.map((e) => row(e, 1)).join('');
    }
    if (!list.length) html += '<div class="empty">No matches</div>';
    tree.innerHTML = html;
    const ri = $('.rename input', tree);
    if (ri) {
      ri.focus(); ri.select();
      const done = (ok) => { const e = App.entities.find((x) => x.id === renaming); renaming = null; if (ok) App.renameEntity(e, ri.value.trim()); else render(); };
      ri.addEventListener('keydown', (ev) => { ev.stopPropagation(); if (ev.key === 'Enter') done(true); if (ev.key === 'Escape') done(false); });
      ri.addEventListener('blur', () => renaming && done(true));
    }
  }
  const entityOf = (el) => App.entities.find((e) => e.id === +el?.dataset.id);
  tree.addEventListener('click', (ev) => {
    const r = ev.target.closest('.tree-row');
    if (!r) return;
    if (r.hasAttribute('data-root') || r.dataset.group) { const k = r.dataset.group || 'root'; collapsed.has(k) ? collapsed.delete(k) : collapsed.add(k); render(); return; }
    const e = entityOf(r);
    if (!e) return;
    if (ev.target.closest('.vis')) { e.hidden = !e.hidden; App.refresh('all'); return; }
    if (ev.target.closest('.lock')) { e.locked = !e.locked; render(); return; }
    if (ev.target.closest('.rename')) return;
    if (ev.shiftKey && anchor) {
      const ids = $$('.tree-row[data-id]', tree).map((x) => +x.dataset.id);
      const a = ids.indexOf(anchor), b = ids.indexOf(e.id);
      App.select(ids.slice(Math.min(a, b), Math.max(a, b) + 1).filter((x) => x !== e.id).concat(e.id));
    } else if (ev.metaKey || ev.ctrlKey) App.select([e.id], 'toggle');
    else { App.select([e.id]); anchor = e.id; }
    $(`.tree-row[data-id="${e.id}"]`, tree)?.focus();
  });
  tree.addEventListener('dblclick', (ev) => { const e = entityOf(ev.target.closest('.tree-row')); if (e && !ev.target.closest('.toggle')) { renaming = e.id; render(); } });
  tree.addEventListener('keydown', (ev) => {
    const rows = $$('.tree-row[data-id]', tree);
    const i = rows.indexOf(document.activeElement);
    if (ev.key === 'ArrowDown' || ev.key === 'ArrowUp') {
      ev.preventDefault(); ev.stopPropagation();
      const n = rows[clamp(i + (ev.key === 'ArrowDown' ? 1 : -1), 0, rows.length - 1)];
      if (!n) return;
      const e = entityOf(n);
      App.select(ev.shiftKey ? [e.id] : [e.id], ev.shiftKey ? 'extend' : undefined);
      $(`.tree-row[data-id="${e.id}"]`, tree)?.focus();
    }
    if (ev.key === 'Enter' || ev.key === 'F2') { ev.preventDefault(); ev.stopPropagation(); const e = entityOf(document.activeElement); if (e) { renaming = e.id; render(); } }
  });
  tree.addEventListener('contextmenu', (ev) => {
    const e = entityOf(ev.target.closest('.tree-row'));
    if (!e) { ev.preventDefault(); return; }
    if (!App.State.selection.includes(e.id)) App.select([e.id]);
    const sel = App.selected();
    Kit.contextMenu(ev, [
      { label: 'Rename', kbd: 'F2', icon: 'edit', disabled: sel.length !== 1, run: () => { renaming = e.id; render(); } },
      { label: 'Duplicate', kbd: 'Shift+D', icon: 'copy', disabled: !!App.State.runtime, run: () => App.duplicateEntities(sel) },
      '-',
      { label: 'Open Prototype', icon: 'prototype', run: () => App.openDoc('prototype', e.prototype) },
      { label: 'Select Same Prototype', run: () => App.select(App.entities.filter((x) => x.prototype === e.prototype).map((x) => x.id)) },
      { label: 'Behaviors', icon: 'graph', disabled: !e.behaviors.length, items: e.behaviors.map((b) => ({ label: b, icon: 'graph', run: () => App.openDoc('behavior', b) })) },
      '-',
      { label: 'Frame', kbd: 'F', icon: 'frame', run: () => App.frame?.('selected') },
      { label: e.hidden ? 'Show' : 'Hide', kbd: 'H', icon: e.hidden ? 'eye' : 'eyeOff', run: () => { sel.forEach((x) => (x.hidden = !e.hidden)); App.refresh('all'); } },
      { label: 'Delete', kbd: 'X', icon: 'trash', disabled: !!App.State.runtime, run: () => App.deleteEntities(sel) },
    ]);
  });
  App.rename = () => { const e = App.activeEntity(); if (e) { renaming = e.id; render(); } };
  return {
    hints: () => [['icon:mouse', 'Select'], ['Mod', 'Toggle'], ['Shift', 'Range'], ['F2', 'Rename'], ['icon:eye', 'Visibility']],
    key(e) {
      const k = e.key.toLowerCase();
      if ((e.metaKey || e.ctrlKey) && k === 'f') { input.focus(); input.select(); return true; }
      if (k === 'x' || e.key === 'Delete') { if (!App.State.runtime) App.deleteEntities(App.selected()); return true; }
      if (k === 'f' || e.key === '.') { App.frame?.('selected'); return true; }
      if (k === 'h') { App.selected().forEach((x) => (x.hidden = !e.altKey)); App.refresh('all'); return true; }
      return false;
    },
    update(what) { if (['runtime-tick', 'layout', 'live', 'log'].includes(what)) return; if (document.activeElement !== input) input.value = App.State.outlinerFilter; $('.clear', search).hidden = !input.value; render(); },
  };
};
App.SHAPES = SHAPES;
})();
