/* Properties (Blender context tabs + panels), Assets and Output editors. */
'use strict';
(() => {
const { $, $$, h, clamp } = Kit;

/* Component → Properties context. Native: reflected components declare a category. */
const CATEGORY = {
  GameTransform: 'object', GameOrientation: 'object', GameAppearance: 'object', GameGlobalBinding: 'object', GameCamera: 'object', GameShader: 'object',
  GameBody: 'physics', GameCollider: 'physics', GameVelocity: 'physics', GameDrive: 'physics', GameFluidVolume: 'physics', GameDestructible: 'physics', MaterialBreakProfile: 'physics', GameSubstance: 'physics', EngineSystem: 'physics',
  GameMaterial: 'material', GameReflection: 'material', GameFragmentGlow: 'material', MaterialTerrain: 'material', GameHRC2D: 'material', GameStarfield2D: 'material', MaterialFluidWorld: 'material', MaterialDestructionWorld: 'material',
  GameHealth: 'gameplay', GameMovement: 'gameplay', GameWeapon: 'gameplay', GameAim: 'gameplay', GameInput: 'gameplay', GameActionBinding: 'gameplay', GameFuel: 'gameplay', GameCombustion: 'gameplay',
};
const TABS = [
  { id: 'scene', icon: 'sceneSettings', label: 'Scene' },
  { id: 'object', icon: 'object', label: 'Object' },
  { id: 'physics', icon: 'physics', label: 'Physics' },
  { id: 'material', icon: 'material', label: 'Material' },
  { id: 'gameplay', icon: 'gameplay', label: 'Gameplay' },
  { id: 'behaviors', icon: 'graph', label: 'Behaviors' },
  { id: 'node', icon: 'fn', label: 'Node' },
];
const ENUMS = {
  'GameBody.type': ['Static', 'Dynamic', 'Kinematic'],
  'GameCollider.shape': ['Circle', 'Box'],
  'GameAppearance.shape': ['Box', 'Circle', 'Diamond', 'Chevron', 'Stroked chevron', 'Capsule', 'Triangle', 'Stroked triangle'],
  'GameActionBinding.source': ['None', 'Mouse left', 'Mouse right', 'Space', 'Shift', 'Tab', 'Enter', ...'ABCDEFGHIJKLMNOPQRSTUVWXYZ'],
};
const COLORS = /(^|_)(color|albedo)$/;
const BOUNDED = { antialias: [0, 1], absorption: [0, 1], friction: [0, 1], restitution: [0, 1], edge_light: [0, 1], emission_intensity: [0, 4], emission_multiplier: [0, 4] };
const INTS = /^(current|maximum|damage|layer|category|mask|filter|border_filter|scene|border_scene|workers|substeps)$/;
const UNITS = { speed: 'u/s', acceleration: 'u/s²', cooldown: 's', lifetime: 's', contact_hertz: 'Hz', radius: 'u', glow_radius: 'u' };
const u32ToColor = (u) => ({ r: (u >>> 24) & 255, g: (u >>> 16) & 255, b: (u >>> 8) & 255, a: (u & 255) / 255 });
const colorToU32 = (c) => ((Math.round(c.r) << 24) | (Math.round(c.g) << 16) | (Math.round(c.b) << 8) | Math.round(c.a * 255)) >>> 0;
const ORDER = ['GameTransform', 'GameOrientation', 'GameAppearance', 'GameCamera', 'GameBody', 'GameCollider', 'GameVelocity', 'GameDrive', 'GameMovement', 'GameHealth', 'GameWeapon', 'GameAim'];
const rank = (c) => { const i = ORDER.indexOf(c); return i < 0 ? 99 : i; };
const VECTOR_LABEL = { GameTransform: 'Position', GameVelocity: 'Velocity' };
const LABEL = { radians: 'Rotation', angular_velocity: 'Angular velocity' };
const label = (k) => LABEL[k] || App.human(k);
const isVectorComponent = (obj) => obj && Object.keys(obj).length === 2 && 'x' in obj && 'y' in obj;
const sceneSettings = { presentation: App.F.scene.presentation, physics: App.clone(App.F.scene.physics) };

App.EDITORS.properties = (area, head, body) => {
  head.insertAdjacentHTML('beforeend', '<div class="field search" style="flex:1"></div>');
  const search = $('.search', head);
  search.innerHTML = `<span class="part addon">${icon('search', 'sm')}</span><input class="part value" placeholder="Search properties" spellcheck="false"><span class="part clear" hidden>${icon('close', 'sm')}</span>`;
  const input = $('input', search);
  input.addEventListener('input', () => { App.State.propSearch = input.value; render(true); });
  input.addEventListener('keydown', (e) => { e.stopPropagation(); if (e.key === 'Escape') { input.value = ''; App.State.propSearch = ''; render(true); input.blur(); } });
  $('.clear', search).addEventListener('click', () => { input.value = ''; App.State.propSearch = ''; render(true); });
  body.innerHTML = '<div class="properties"><nav class="prop-tabs" role="tablist"></nav><div class="prop-main"><div class="prop-scroll scroll"></div></div></div>';
  const tabsEl = $('.prop-tabs', body), scroll = $('.prop-scroll', body);
  let controls = [], structure = '';

  const q = () => App.State.propSearch.trim().toLowerCase();
  const matches = (label) => !q() || label.toLowerCase().includes(q());
  const hl = (label) => { const s = q(); if (!s) return label; const i = label.toLowerCase().indexOf(s); return i < 0 ? label : `${label.slice(0, i)}<span class="hl">${label.slice(i, i + s.length)}</span>${label.slice(i + s.length)}`; };
  const locked = () => !!App.State.runtime;

  function fieldsOf(obj, prefix = '') {
    const out = [];
    for (const [k, v] of Object.entries(obj || {})) {
      const path = prefix ? `${prefix}.${k}` : k;
      if (v && typeof v === 'object' && !Array.isArray(v) && !('x' in v && 'y' in v && Object.keys(v).length === 2)) out.push(...fieldsOf(v, path));
      else out.push({ key: k, path, sample: v });
    }
    return out;
  }
  function availableTabs(s) {
    const tabs = new Set(['scene']);
    if (s) { tabs.add('object'); for (const c of App.subjectComponents(s)) tabs.add(CATEGORY[c] || 'object'); if (s.kind === 'scene') tabs.add('behaviors'); }
    if (App.State.focus === 'graph' && App.State.graph.selected.length) tabs.add('node');
    return TABS.filter((t) => tabs.has(t.id));
  }
  function tabMatches(tab, s) {
    if (!q()) return true;
    if (tab === 'scene') return Object.keys(sceneSettings.physics).some((k) => matches(App.human(k))) || matches('presentation');
    if (tab === 'behaviors') return (s?.entity?.behaviors || []).some(matches);
    if (tab === 'node') return true;
    return App.subjectComponents(s || {}).filter((c) => (CATEGORY[c] || 'object') === tab).some((c) => matches(App.human(c)) || fieldsOf(protoOf(s, c)).some((f) => matches(App.human(f.key))));
  }
  const protoOf = (s, c) => (s.kind === 'prototype' ? App.prototypes[s.name][c] : { ...(App.prototypes[s.entity.prototype]?.[c] || {}), ...(s.entity.overrides[c] || {}) });

  /* Live edits: scrubs and pickers write straight to the model for immediate feedback;
     the commit restores the pre-gesture state and records one undoable change. */
  function liveEdit(s, comp, path) {
    let snap = null;
    const target = () => (s.kind === 'prototype' ? App.prototypes[s.name] : s.entity.overrides);
    return {
      change(v) {
        if (!snap) snap = { override: App.overridden(s, comp, path), value: App.read(s, comp, path), comp: App.clone(target()[comp] ?? null) };
        const t = target();
        t[comp] ??= {};
        path.split('.').reduce((o, k, i, a) => (i === a.length - 1 ? (o[k] = v) : (o[k] ??= {})), t[comp]);
        App.refresh('live');
      },
      commit(v, before) {
        const b = snap ? snap.value : before;
        if (snap) { const t = target(); if (snap.comp === null) delete t[comp]; else t[comp] = snap.comp; }
        snap = null;
        if (v !== b) App.setValue(s, comp, path, v, b);
      },
    };
  }
  function valueMenu(s, comp, path, get) {
    return () => [
      { label: 'Reset to Prototype', icon: 'reset', disabled: !App.overridden(s, comp, path) || locked(), run: () => App.resetOverride(s, comp, path) },
      { label: `Apply to ${s.entity?.prototype || 'Prototype'}`, icon: 'prototype', disabled: !App.overridden(s, comp, path) || locked(), run: () => App.applyToPrototype(s, comp, path) },
      '-',
      { label: 'Copy Value', kbd: 'Mod+C', icon: 'copy', run: () => { navigator.clipboard?.writeText(String(get())); App.report('Copied value'); } },
      { label: 'Copy Data Path', icon: 'link', run: () => { navigator.clipboard?.writeText(`${comp}.${path}`); App.report(`Copied ${comp}.${path}`); } },
    ];
  }

  function control(s, comp, f) {
    const path = f.path, key = f.key, full = `${comp}.${key}`;
    const get = () => App.read(s, comp, path);
    const ov = () => App.overridden(s, comp, path);
    const live = liveEdit(s, comp, path);
    const menu = valueMenu(s, comp, path, get);
    const v = f.vectorComponent ? { x: App.read(s, comp, 'x'), y: App.read(s, comp, 'y') } : get();
    const el = h('<div class="control"></div>');
    let sync = () => {};
    if (f.vectorComponent || (v && typeof v === 'object' && 'x' in v)) {
      const sub = (axis) => (f.vectorComponent ? axis : `${path}.${axis}`);
      const getAxis = (axis) => App.read(s, comp, sub(axis));
      const j = h('<div class="joined"></div>');
      el.appendChild(j);
      const parts = ['x', 'y'].map((axis) => {
        const f2 = h('<div></div>'); j.appendChild(f2);
        const lv = liveEdit(s, comp, sub(axis));
        return Kit.numberField(f2, { value: getAxis(axis) ?? 0, addon: axis.toUpperCase(), addonClass: axis, step: 1, precision: 2, onChange: lv.change, onCommit: lv.commit, overridden: () => App.overridden(s, comp, sub(axis)), readOnly: locked, menu: valueMenu(s, comp, sub(axis), () => getAxis(axis)), reset: () => App.overridden(s, comp, sub(axis)) && App.resetOverride(s, comp, sub(axis)) });
      });
      sync = () => parts.forEach((p, i) => { p.setValue(getAxis(['x', 'y'][i]) ?? 0); p.el.toggleAttribute('data-readonly', locked()); });
    } else if (typeof v === 'boolean') {
      const c = h('<span></span>'); el.appendChild(c);
      const cb = Kit.checkbox(c, { value: get, onChange: (nv) => App.setValue(s, comp, path, nv, !nv), readOnly: locked });
      sync = cb.refresh;
    } else if (v === null) {
      const f2 = h(`<div class="field select left" tabindex="0">${icon('target')}<span class="part value" style="color:var(--fg-3)">None</span>${icon('eyedropper', 'caret')}</div>`);
      el.appendChild(f2);
      f2.addEventListener('click', () => App.palette({ label: App.human(key), items: () => App.entities.map((e) => ({ id: `pick${e.id}`, group: 'Objects', label: e.name, icon: App.protoIcon(e.prototype), action: 'Pick', run: () => App.report(`${App.human(key)}: ${e.name}`) })) }));
    } else if (typeof v === 'string') {
      const f2 = h(`<div class="field text left"><input class="part value" spellcheck="false"></div>`); el.appendChild(f2);
      const inp = $('input', f2);
      inp.value = v;
      inp.addEventListener('keydown', (e) => { e.stopPropagation(); if (e.key === 'Enter') inp.blur(); if (e.key === 'Escape') { inp.value = get(); inp.blur(); } });
      inp.addEventListener('focus', () => f2.setAttribute('data-editing', ''));
      inp.addEventListener('blur', () => { f2.removeAttribute('data-editing'); if (inp.value !== get()) App.setValue(s, comp, path, inp.value, get()); });
      sync = () => { if (document.activeElement !== inp) inp.value = get(); inp.readOnly = locked(); };
    } else if (COLORS.test(key)) {
      const f2 = h('<div></div>'); el.appendChild(f2);
      let startColor = null;
      const cf = Kit.colorField(f2, {
        value: () => u32ToColor(get()), overridden: ov, readOnly: locked, menu,
        onChange: (c, reason) => { if (startColor === null) startColor = get(); if (reason === 'cancel') { live.change(startColor); } else live.change(colorToU32(c)); },
        onCommit: () => { const nv = get(); if (startColor !== null) live.commit(nv, startColor); startColor = null; },
      });
      const hex = h(`<span class="dim num" style="width:62px;text-align:right;font-size:11px"></span>`); el.appendChild(hex);
      sync = () => { cf.refresh(); const c = u32ToColor(get()); hex.textContent = c.a === 0 ? 'None' : Kit.hex(c).toUpperCase(); };
    } else if (ENUMS[full]) {
      const opts = ENUMS[full].map((label, value) => ({ value, label }));
      const f2 = h('<div></div>'); el.appendChild(f2);
      const ctl = opts.length <= 3
        ? Kit.toggleGroup(f2, { options: opts, value: get, onChange: (nv) => App.setValue(s, comp, path, nv, get()), readOnly: locked })
        : Kit.select(f2, { options: opts, value: get, onChange: (nv) => App.setValue(s, comp, path, nv, get()), readOnly: locked });
      if (opts.length <= 3) f2.classList.add('fill'), (f2.style.flex = '1');
      sync = ctl.refresh;
    } else if (key === 'radians') {
      const f2 = h('<div></div>'); el.appendChild(f2);
      const deg = (r) => +(r * 180 / Math.PI).toFixed(3), rad = (d) => +(d * Math.PI / 180).toFixed(6);
      const nf = Kit.numberField(f2, { value: deg(v || 0), unit: '°', step: 1, precision: 1, overridden: ov, readOnly: locked, menu, onChange: (d) => live.change(rad(d)), onCommit: (d, b) => live.commit(rad(d), rad(b)) });
      sync = () => { nf.setValue(deg(get() || 0)); f2.toggleAttribute('data-readonly', locked()); };
    } else if (typeof v === 'number') {
      const b = BOUNDED[key], int = INTS.test(key);
      const f2 = h('<div></div>'); el.appendChild(f2);
      const nf = Kit.numberField(f2, {
        value: v, step: int ? 1 : b ? 0.01 : Math.abs(v) >= 100 ? 1 : 0.1, precision: int ? 0 : b ? 3 : 2, integer: int,
        min: b ? b[0] : -Infinity, max: b ? b[1] : Infinity, slider: !!b, unit: UNITS[key] || '',
        overridden: ov, readOnly: locked, menu, onChange: live.change, onCommit: live.commit,
        reset: () => ov() && App.resetOverride(s, comp, path),
      });
      f2.addEventListener('paste-value', (e) => App.setValue(s, comp, path, e.detail, get()));
      sync = () => { nf.setValue(get()); f2.toggleAttribute('data-readonly', locked()); };
    }
    sync();
    controls.push(sync);
    return el;
  }

  function componentPanel(s, comp) {
    const all = isVectorComponent(protoOf(s, comp)) ? [{ key: VECTOR_LABEL[comp] || 'Value', path: '', vectorComponent: true }] : fieldsOf(protoOf(s, comp));
    const compMatch = matches(App.human(comp));
    const rows = all.filter((f) => compMatch || matches(label(f.key)));
    if (q() && !rows.length) return null;
    const open = q() ? true : !!App.State.openPanels[comp];
    const n = App.componentOverrides(s, comp);
    const p = h(`<section class="panel" ${open ? 'data-open' : ''}>
      <header class="panel-head" aria-expanded="${open}">${icon('down', 'chev')}<span class="title">${hl(App.human(comp))}</span>${n ? `<span class="badge override" data-tip="Instance overrides">${n}</span>` : ''}<span class="spacer"></span><span class="grip">${icon('grip')}</span><button class="btn sm icon-only more" data-stop data-tip="${App.human(comp)}">${icon('more')}</button></header>
      <div class="panel-body"></div></section>`);
    Kit.panel(p, { onToggle: (o) => { if (!q()) App.State.openPanels[comp] = o; } });
    $('.more', p).addEventListener('click', (e) => Kit.openMenu([
      { label: 'Reset Overrides', icon: 'reset', disabled: !n || locked(), run: () => App.resetOverride(s, comp) },
      { label: 'Copy Values', icon: 'copy', run: () => { navigator.clipboard?.writeText(JSON.stringify(protoOf(s, comp))); App.report(`Copied ${App.human(comp)}`); } },
      '-',
      { label: 'Remove Component', icon: 'trash', disabled: s.kind === 'scene', run: () => App.report('Removed') },
    ], e.currentTarget, { anchor: e.currentTarget, place: { side: 'bottom', align: 'end', offset: 2 } }));
    const bodyEl = $('.panel-body', p);
    if (open) for (const f of rows) {
      const r = h(`<div class="prop" ${q() && matches(label(f.key)) ? 'data-match' : ''}><span class="label"><span>${hl(label(f.key))}</span></span></div>`);
      r.appendChild(control(s, comp, f));
      bodyEl.appendChild(r);
    }
    return p;
  }
  function identity(s) {
    const el = h('<div class="identity"></div>');
    if (s.kind === 'prototype') {
      const users = App.entities.filter((e) => e.prototype === s.name).length;
      el.innerHTML = `<span class="glyph">${icon(App.protoIcon(s.name))}</span><div class="who"><div class="row" style="gap:6px"><b style="font-weight:600;font-size:13px">${s.name}</b><span class="badge accent">Prototype</span></div><div class="lineage">${Object.keys(App.prototypes[s.name]).length} components · ${users} ${users === 1 ? 'instance' : 'instances'}</div></div>`;
      return el;
    }
    const e = s.entity, n = App.subjectComponents(s).reduce((a, c) => a + App.componentOverrides(s, c), 0);
    el.innerHTML = `<span class="glyph">${icon(App.protoIcon(e.prototype))}</span><div class="who"><div class="field text left name"><input class="part value" spellcheck="false"></div><div class="lineage"><span>Instance of</span><button class="btn proto">${icon('prototype')}<span>${e.prototype}</span>${icon('external')}</button>${n ? `<span class="spacer"></span><button class="btn ov"><span class="badge override">${n} ${n === 1 ? 'override' : 'overrides'}</span></button>` : ''}</div></div>`;
    const inp = $('input', el);
    inp.value = e.name;
    inp.addEventListener('keydown', (ev) => { ev.stopPropagation(); if (ev.key === 'Enter') inp.blur(); if (ev.key === 'Escape') { inp.value = e.name; inp.blur(); } });
    inp.addEventListener('focus', () => inp.parentElement.setAttribute('data-editing', ''));
    inp.addEventListener('blur', () => { inp.parentElement.removeAttribute('data-editing'); App.renameEntity(e, inp.value.trim()); });
    $('.proto', el).addEventListener('click', () => App.openDoc('prototype', e.prototype));
    $('.ov', el)?.addEventListener('click', (ev) => Kit.openMenu([{ label: 'Reset All Overrides', icon: 'reset', disabled: locked(), run: () => Object.keys(e.overrides).forEach((c) => App.resetOverride(s, c)) }], ev.currentTarget, { anchor: ev.currentTarget, place: { side: 'bottom', align: 'end', offset: 2 } }));
    return el;
  }
  function scenePanels() {
    const frag = document.createDocumentFragment();
    const pres = h(`<section class="panel" ${App.State.openPanels.sceneSettings ? 'data-open' : ''}><header class="panel-head">${icon('down', 'chev')}<span class="title">Presentation</span></header><div class="panel-body"></div></section>`);
    Kit.panel(pres, { onToggle: (o) => (App.State.openPanels.sceneSettings = o) });
    if (matches('presentation') || matches('mode')) {
      const r = h('<div class="prop"><span class="label">Mode</span><div class="control"></div></div>');
      const tg = h('<div></div>'); $('.control', r).appendChild(tg);
      Kit.toggleGroup(tg, { options: [{ value: '2d', label: '2D' }, { value: '3d', label: '3D' }], value: () => sceneSettings.presentation, onChange: (v) => { sceneSettings.presentation = v; App.report(`Presentation ${v.toUpperCase()}`); }, readOnly: locked });
      tg.style.flex = '1'; tg.classList.add('fill');
      $('.panel-body', pres).appendChild(r);
      frag.appendChild(pres);
    }
    const phys = h(`<section class="panel" ${App.State.openPanels.scenePhysics || q() ? 'data-open' : ''}><header class="panel-head">${icon('down', 'chev')}<span class="title">Physics</span></header><div class="panel-body"></div></section>`);
    Kit.panel(phys, { onToggle: (o) => (App.State.openPanels.scenePhysics = o) });
    let any = false;
    for (const [k, v] of Object.entries(sceneSettings.physics)) {
      if (!matches(App.human(k)) && !matches('physics')) continue;
      any = true;
      const r = h(`<div class="prop"><span class="label"><span>${hl(App.human(k))}</span></span><div class="control"></div></div>`);
      const c = $('.control', r), f2 = h('<div></div>');
      c.appendChild(f2);
      if (typeof v === 'boolean') Kit.checkbox(f2, { value: () => sceneSettings.physics[k], onChange: (nv) => { sceneSettings.physics[k] = nv; }, readOnly: locked });
      else Kit.numberField(f2, { value: v, step: INTS.test(k) ? 1 : 0.5, integer: INTS.test(k), precision: INTS.test(k) ? 0 : 1, min: 0, unit: UNITS[k] || '', onCommit: (nv) => { sceneSettings.physics[k] = nv; }, readOnly: locked });
      $('.panel-body', phys).appendChild(r);
    }
    if (any) frag.appendChild(phys);
    return frag;
  }
  function behaviorsPanel(s) {
    const e = s.entity;
    const p = h(`<section class="panel" data-open><header class="panel-head">${icon('down', 'chev')}<span class="title">Behaviors</span><span class="count">${e.behaviors.length}</span></header><div class="panel-body"></div></section>`);
    Kit.panel(p);
    const b = $('.panel-body', p);
    for (const name of e.behaviors.filter(matches)) {
      const beh = App.F.behaviors[name];
      const r = h(`<div class="behavior-row" data-tip="${beh?.description || name}">${icon('graph')}<span class="name">${hl(name)}</span><span class="graphs">${beh?.graphs.length || 0} graphs</span><button class="btn sm icon-only open" data-tip="Open">${icon('external')}</button><button class="btn sm icon-only rm" data-tip="Detach">${icon('close')}</button></div>`);
      $('.open', r).addEventListener('click', () => App.openDoc('behavior', name));
      $('.rm', r).addEventListener('click', () => App.toggleBehavior(e, name));
      r.addEventListener('dblclick', () => App.openDoc('behavior', name));
      b.appendChild(r);
    }
    const add = h(`<div class="add-row"><button class="btn subtle">${icon('add')}<span>Attach Behavior</span></button></div>`);
    $('button', add).addEventListener('click', () => App.palette('behaviors'));
    b.appendChild(add);
    return p;
  }
  function nodePanels() {
    const gs = App.graphSubject?.();
    const frag = document.createDocumentFragment();
    if (!gs?.nodes.length) { frag.appendChild(h('<div class="prop-empty">No node selected</div>')); return frag; }
    const n = gs.nodes[gs.nodes.length - 1];
    const idEl = h(`<div class="identity"><span class="glyph">${icon(n.type === 'Event' ? 'event' : n.type === 'Branch' ? 'branch' : 'expression')}</span><div class="who"><b style="font-weight:600;font-size:13px">${n.type === 'Event' ? `On ${n.title}` : n.type}</b><div class="lineage">${gs.behavior} · ${gs.graph.kind === 'event' ? 'Event graph' : gs.graph.name}</div></div></div>`);
    frag.appendChild(idEl);
    const mk = (title, pins, dir) => {
      const list = pins.filter((p) => p.kind !== 'exec' && matches(p.name));
      if (!list.length) return;
      const p = h(`<section class="panel" data-open><header class="panel-head">${icon('down', 'chev')}<span class="title">${title}</span><span class="count">${list.length}</span></header><div class="panel-body"></div></section>`);
      Kit.panel(p);
      for (const pin of list) {
        const r = h(`<div class="prop"><span class="label"><span>${hl(pin.name)}</span></span><div class="control"></div></div>`);
        const c = $('.control', r);
        if (dir === 'in' && pin.binding) c.appendChild(h(`<div class="field text left" data-readonly><span class="part addon">${icon('field', 'sm')}</span><span class="part value">${pin.binding}</span></div>`));
        else if (dir === 'in' && typeof pin.default === 'number') { const f2 = h('<div></div>'); c.appendChild(f2); Kit.numberField(f2, { value: pin.default, step: 0.1, onCommit: (v) => { pin.default = v; App.refresh('graph'); } }); }
        else c.appendChild(h(`<span class="badge">${pin.type}</span>`));
        $('.panel-body', p).appendChild(r);
      }
      frag.appendChild(p);
    };
    mk('Inputs', n.inputs, 'in');
    mk('Outputs', n.outputs, 'out');
    return frag;
  }

  function render(force) {
    const s = App.subject();
    let tabs = availableTabs(s);
    if (App.State.focus === 'graph' && App.State.graph.selected.length && App.State.propTab !== 'node' && force === 'graph-selection') App.State.propTab = 'node';
    if (!tabs.some((t) => t.id === App.State.propTab)) App.State.propTab = tabs.find((t) => t.id === 'object')?.id || 'scene';
    const key = JSON.stringify([s?.entity?.id ?? s?.name, App.State.doc, App.State.propTab, App.State.propSearch, tabs.map((t) => t.id), s ? App.subjectComponents(s) : [], s?.entity ? App.subjectComponents(s).map((c) => App.componentOverrides(s, c)) : [], s?.entity?.name, s?.entity?.behaviors, App.State.graph.selected, !!App.State.runtime]);
    if (!force && key === structure) { controls.forEach((f) => f()); return; }
    structure = key;
    tabsEl.innerHTML = tabs.map((t, i) => `${t.id === 'behaviors' || t.id === 'node' ? '<span class="tab-sep"></span>' : ''}<button class="tab" role="tab" data-tab="${t.id}" ${t.id === App.State.propTab ? 'data-active' : ''} ${tabMatches(t.id, s) ? '' : 'data-dim'} data-tip="${t.label}" data-tip-side="right">${icon(t.icon)}${q() && tabMatches(t.id, s) && t.id !== App.State.propTab ? '<span class="match-dot"></span>' : ''}</button>`).join('');
    const top = scroll.scrollTop;
    scroll.innerHTML = '';
    controls = [];
    const tab = App.State.propTab;
    if (tab === 'scene') scroll.appendChild(scenePanels());
    else if (tab === 'node') scroll.appendChild(nodePanels());
    else if (!s) scroll.appendChild(h('<div class="prop-empty">No selection</div>'));
    else {
      if (tab === 'object') scroll.appendChild(identity(s));
      if (tab === 'behaviors') scroll.appendChild(behaviorsPanel(s));
      else for (const c of App.subjectComponents(s).filter((c) => (CATEGORY[c] || 'object') === tab).sort((a, b) => rank(a) - rank(b))) { const p = componentPanel(s, c); if (p) scroll.appendChild(p); }
    }
    if (q() && !scroll.children.length) scroll.appendChild(h('<div class="prop-empty">No matching properties</div>'));
    $$('.field', scroll).forEach((f) => f.toggleAttribute('data-readonly', locked() && !f.closest('.identity')));
    scroll.scrollTop = top;
  }
  tabsEl.addEventListener('click', (e) => { const t = e.target.closest('.tab'); if (t) { App.State.propTab = t.dataset.tab; render(true); } });
  tabsEl.addEventListener('keydown', (e) => {
    const tabs = $$('.tab', tabsEl), i = tabs.indexOf(document.activeElement);
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); e.stopPropagation(); const n = tabs[clamp(i + (e.key === 'ArrowDown' ? 1 : -1), 0, tabs.length - 1)]; App.State.propTab = n.dataset.tab; render(true); $(`.tab[data-tab="${n.dataset.tab}"]`, tabsEl).focus(); }
  });
  return {
    hints: () => [['icon:mouse', 'Drag to scrub'], ['Alt', 'Fine'], ['Shift', 'Coarse'], ['Ctrl', 'Snap'], ['Mod+C', 'Copy'], ['Mod+V', 'Paste'], ['icon:mouse', 'Context menu (right)']],
    key(e) {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'f') { input.focus(); input.select(); return true; }
      return false;
    },
    update(what) {
      if (what === 'live' || what === 'layout') return;
      if (document.activeElement !== input) input.value = App.State.propSearch;
      $('.clear', search).hidden = !input.value;
      if (what === 'runtime-tick') { controls.forEach((f) => f()); return; }
      render(what === 'graph-selection' ? 'graph-selection' : false);
    },
  };
};

/* ---------- Assets ---------- */
App.EDITORS.assets = (area, head, body) => {
  head.insertAdjacentHTML('beforeend', `<div class="field search" style="width:220px"><span class="part addon">${icon('search', 'sm')}</span><input class="part value" placeholder="Filter assets" spellcheck="false"></div><span class="spacer"></span><button class="btn icon-only close" data-tip="Close">${icon('close')}</button>`);
  const input = $('input', head);
  let folder = 'prototypes', selectedAsset = null;
  body.innerHTML = '<div class="assets"><div class="cats scroll"></div><div class="grid scroll"></div></div>';
  const cats = $('.cats', body), grid = $('.grid', body);
  const folders = [...new Set(App.F.assets.map((p) => p.split('/')[0]))];
  const FOLDER_ICON = { behaviors: 'graph', events: 'event', fields: 'grid', libraries: 'library', materials: 'material', prototypes: 'prototype', scenes: 'scene', 'shader-libraries': 'library', systems: 'system' };
  function render() {
    const q = input.value.trim().toLowerCase();
    cats.innerHTML = folders.map((f) => `<div class="tree-row" data-folder="${f}" ${f === folder && !q ? 'data-active' : ''} style="padding-left:6px">${icon(FOLDER_ICON[f] || 'folder')}<span class="name">${App.human(f.replace(/-/g, ' '))}</span><span class="meta num">${App.F.assets.filter((p) => p.startsWith(`${f}/`)).length}</span></div>`).join('');
    const list = App.F.assets.filter((p) => (q ? p.toLowerCase().includes(q) : p.startsWith(`${folder}/`)));
    grid.innerHTML = list.map((p) => {
      const name = p.split('/').pop().replace(/\.json$/, ''), f = p.split('/')[0];
      const ic = f === 'prototypes' ? App.protoIcon(name) : FOLDER_ICON[f] || 'json';
      return `<div class="asset-tile" data-path="${p}" ${p === selectedAsset ? 'data-selected' : ''} draggable="${f === 'prototypes' && App.prototypes[name]?.GameTransform ? 'true' : 'false'}" data-tip="${p}"><span class="thumb">${icon(ic)}</span><span class="label">${name}</span></div>`;
    }).join('') || '<div class="prop-empty" style="width:100%">No assets</div>';
  }
  const open = (p) => {
    const [f, file] = [p.split('/')[0], p.split('/').pop().replace(/\.json$/, '')];
    const kind = { scenes: 'scene', behaviors: 'behavior', prototypes: 'prototype' }[f];
    if (kind) App.openDoc(kind, file); else App.report(`${file} opens in its asset editor`, 'info');
  };
  cats.addEventListener('click', (e) => { const r = e.target.closest('[data-folder]'); if (r) { folder = r.dataset.folder; input.value = ''; render(); } });
  grid.addEventListener('click', (e) => { const t = e.target.closest('.asset-tile'); if (t) { selectedAsset = t.dataset.path; render(); } });
  grid.addEventListener('dblclick', (e) => { const t = e.target.closest('.asset-tile'); if (t) open(t.dataset.path); });
  grid.addEventListener('dragstart', (e) => { const t = e.target.closest('.asset-tile'); if (!t) return; e.dataTransfer.setData('chibi/prototype', t.dataset.path.split('/').pop().replace(/\.json$/, '')); e.dataTransfer.effectAllowed = 'copy'; });
  grid.addEventListener('contextmenu', (e) => {
    const t = e.target.closest('.asset-tile');
    if (!t) { e.preventDefault(); return; }
    const p = t.dataset.path, name = p.split('/').pop().replace(/\.json$/, ''), proto = p.startsWith('prototypes/') && App.prototypes[name]?.GameTransform;
    Kit.contextMenu(e, [{ label: 'Open', run: () => open(p) }, { label: 'Add to Scene', icon: 'add', disabled: !proto || !!App.State.runtime, run: () => App.addEntity(name, App.viewCenter?.() || { x: 0, y: 0 }) }, '-', { label: 'Copy Path', icon: 'copy', run: () => App.report(`Copied ${p}`) }]);
  });
  input.addEventListener('input', render);
  input.addEventListener('keydown', (e) => e.stopPropagation());
  $('.close', head).addEventListener('click', () => App.layout.drawer(null));
  return { hints: () => [['icon:mouse', 'Select'], ['icon:mouse', 'Open (double-click)'], ['icon:move', 'Drag prototype into Scene']], update(what) { if (what === 'all' || what === 'drawer') render(); } };
};

/* ---------- Output ---------- */
const LOG = [
  { t: '14:52:03', level: 'info', text: 'Loaded Neon Swarm · 24 prototypes · 14 behaviors' },
  { t: '14:52:03', level: 'info', text: 'Compiled 14 behaviors · 0 errors' },
  { t: '14:52:04', level: 'warn', text: 'Weapon · Fire: cooldown 0.125 s shorter than two frames at 30 Hz', link: 'Weapon' },
  { t: '14:52:04', level: 'info', text: 'HRC 2D · 4 cascades · 512 probes' },
];
App.log = (level, text, link) => { const d = new Date(); LOG.push({ t: d.toTimeString().slice(0, 8), level, text, link }); App.refresh('log'); };
App.EDITORS.output = (area, head, body) => {
  head.insertAdjacentHTML('beforeend', `<span class="levels"></span><div class="field search" style="width:200px;margin-left:6px"><span class="part addon">${icon('search', 'sm')}</span><input class="part value" placeholder="Filter" spellcheck="false"></div><span class="spacer"></span><button class="btn clear-log">${icon('trash')}<span>Clear</span></button><button class="btn icon-only close" data-tip="Close">${icon('close')}</button>`);
  let level = 'all';
  const input = $('input', head);
  const lv = Kit.toggleGroup($('.levels', head), { options: [{ value: 'all', label: 'All' }, { value: 'warn', label: 'Warnings' }, { value: 'error', label: 'Errors' }], value: () => level, onChange: (v) => { level = v; render(); } });
  body.innerHTML = '<div class="output"><div class="log scroll"></div></div>';
  const log = $('.log', body);
  function render() {
    const q = input.value.toLowerCase();
    const rows = LOG.filter((r) => (level === 'all' || r.level === level) && (!q || r.text.toLowerCase().includes(q)));
    log.innerHTML = rows.map((r) => `<div class="log-row" data-level="${r.level}" ${r.link ? `data-link="${r.link}"` : ''}><span class="time">${r.t}</span>${icon(r.level === 'warn' ? 'warning' : r.level === 'error' ? 'error' : 'info')}<span class="${r.link ? 'link' : ''}">${r.text}</span></div>`).join('') || '<div class="prop-empty">No messages</div>';
    log.scrollTop = log.scrollHeight;
  }
  log.addEventListener('click', (e) => { const r = e.target.closest('[data-link]'); if (r) App.openDoc('behavior', r.dataset.link); });
  input.addEventListener('input', render);
  input.addEventListener('keydown', (e) => e.stopPropagation());
  $('.clear-log', head).addEventListener('click', () => { LOG.length = 0; render(); });
  $('.close', head).addEventListener('click', () => App.layout.drawer(null));
  return { hints: () => [['icon:mouse', 'Open source']], update(what) { if (what !== 'runtime-tick' && what !== 'live' && what !== 'layout') { lv.refresh(); render(); } } };
};
})();
