/* Studio kit behaviors. Each component follows a Base UI contract (parts, data-*
   state, keyboard model, change vs. commit) so the native port has one reference. */
'use strict';
(() => {
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const h = (html) => { const t = document.createElement('template'); t.innerHTML = html.trim(); return t.content.firstElementChild; };
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const isMac = /Mac|iPhone/.test(navigator.platform);
const mod = isMac ? '⌘' : 'Ctrl+';
const kbd = (s) => (s || '').replace(/Mod\+?/g, mod).replace(/Shift\+?/g, isMac ? '⇧' : 'Shift+').replace(/Alt\+?/g, isMac ? '⌥' : 'Alt+');
const reducedMotion = () => document.documentElement.dataset.motion === 'reduced';

/* ---------- Presence: data-starting-style / data-ending-style (Base UI) ---------- */
function mount(el, parent = document.body) {
  el.setAttribute('data-starting-style', '');
  parent.appendChild(el);
  requestAnimationFrame(() => requestAnimationFrame(() => el.removeAttribute('data-starting-style')));
}
function unmount(el) {
  if (!el.isConnected) return;
  if (reducedMotion()) { el.remove(); return; }
  el.setAttribute('data-ending-style', '');
  setTimeout(() => el.remove(), 100);
}

/* ---------- Positioner: side/align with collision flip ---------- */
function place(el, anchor, { side = 'bottom', align = 'start', offset = 4 } = {}) {
  const a = anchor instanceof Element ? anchor.getBoundingClientRect() : { left: anchor.x, right: anchor.x, top: anchor.y, bottom: anchor.y, width: 0, height: 0 };
  const r = el.getBoundingClientRect();
  const vw = innerWidth, vh = innerHeight;
  let x, y, origin;
  if (side === 'bottom' || side === 'top') {
    x = align === 'start' ? a.left : align === 'end' ? a.right - r.width : a.left + a.width / 2 - r.width / 2;
    y = side === 'bottom' ? a.bottom + offset : a.top - r.height - offset;
    if (side === 'bottom' && y + r.height > vh - 4) y = a.top - r.height - offset;
    if (side === 'top' && y < 4) y = a.bottom + offset;
    origin = `${align === 'end' ? 'right' : align === 'center' ? 'center' : 'left'} ${y < a.top ? 'bottom' : 'top'}`;
  } else {
    x = side === 'right' ? a.right + offset : a.left - r.width - offset;
    y = align === 'start' ? a.top : a.bottom - r.height;
    if (side === 'right' && x + r.width > vw - 4) x = a.left - r.width - offset;
    origin = `${x < a.left ? 'right' : 'left'} top`;
  }
  el.style.left = `${clamp(x, 4, vw - r.width - 4)}px`;
  el.style.top = `${clamp(y, 4, vh - r.height - 4)}px`;
  el.style.setProperty('--origin', origin);
}

/* ---------- Tooltip: 500 ms delay; instant within the group window ---------- */
const Tooltip = (() => {
  let tip = null, timer = 0, warmUntil = 0, target = null;
  const DELAY = 500, GROUP = 400;
  function show(el) {
    hide(true);
    target = el;
    tip = h(`<div class="tooltip" role="tooltip"><span>${el.dataset.tip}</span>${el.dataset.kbd ? `<span class="kbd">${kbd(el.dataset.kbd)}</span>` : ''}</div>`);
    mount(tip);
    place(tip, el, { side: el.dataset.tipSide || 'bottom', align: 'center', offset: 6 });
  }
  function hide(keepWarm) {
    clearTimeout(timer);
    if (tip) { warmUntil = keepWarm === true ? 0 : performance.now() + GROUP; unmount(tip); tip = null; }
    target = null;
  }
  document.addEventListener('pointerover', (e) => {
    const el = e.target.closest('[data-tip]');
    if (el === target) return;
    if (!el) { if (target) hide(); return; }
    clearTimeout(timer);
    if (tip || performance.now() < warmUntil) show(el);
    else timer = setTimeout(() => show(el), DELAY);
  });
  document.addEventListener('focusin', (e) => {
    const el = e.target.closest?.('[data-tip]');
    if (el && e.target.matches(':focus-visible')) show(el);
  });
  document.addEventListener('focusout', () => hide());
  addEventListener('pointerdown', () => hide(true), true);
  addEventListener('keydown', (e) => { if (e.key !== 'Tab' && !e.key.startsWith('Arrow')) hide(true); }, true);
  addEventListener('wheel', () => hide(true), true);
  return { hide };
})();

/* ---------- Layers: one dismissal stack for menus, popovers and dialogs ---------- */
const Layers = (() => {
  const stack = [];
  function push(layer) { stack.push(layer); return layer; }
  function close(layer, reason = 'programmatic') {
    const i = stack.indexOf(layer);
    if (i < 0) return;
    for (const l of stack.splice(i).reverse()) { unmount(l.el); l.onClose?.(reason); }
  }
  function top() { return stack[stack.length - 1]; }
  addEventListener('pointerdown', (e) => {
    for (let i = stack.length - 1; i >= 0; i--) {
      const l = stack[i];
      if (l.el.contains(e.target) || l.anchor?.contains?.(e.target)) return;
      if (l.keepOnOutside?.(e)) return;
      close(l, 'outside-press');
    }
  }, true);
  addEventListener('keydown', (e) => {
    const l = top();
    if (!l) return;
    if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); close(l, 'escape-key'); l.returnFocus?.focus?.(); return; }
    if (l.onKey?.(e) === true) { e.preventDefault(); e.stopPropagation(); }
  }, true);
  return { push, close, top, get open() { return stack.length > 0; }, stack };
})();

/* ---------- Menu (Base UI Menu / Menubar / Context Menu) ---------- */
function menuHtml(items) {
  return items.map((it, i) => {
    if (it === '-') return '<div class="menu-sep" role="separator"></div>';
    if (it.group) return `<div class="menu-group">${it.group}</div>`;
    const attrs = [`data-index="${i}"`, it.disabled ? 'data-disabled' : '', it.checked ? 'data-checked' : ''].join(' ');
    const check = it.checked !== undefined ? `<span class="check-slot">${icon('check')}</span>` : '';
    const lead = check || (it.icon ? icon(it.icon) : '');
    const trail = it.items ? `<span class="icon chev sm">${String.fromCodePoint(ICONS.right)}</span>` : it.kbd ? `<span class="kbd">${kbd(it.kbd)}</span>` : '';
    return `<div class="menu-item${it.items ? ' sub' : ''}" role="menuitem" ${attrs}>${lead}<span class="label">${it.label}</span>${trail}</div>`;
  }).join('');
}
function openMenu(items, anchor, opts = {}) {
  const el = h(`<div class="popup menu" role="menu">${menuHtml(items)}</div>`);
  if (opts.minWidth) el.style.minWidth = `${opts.minWidth}px`;
  mount(el);
  place(el, anchor, opts.place || { side: 'bottom', align: 'start', offset: 2 });
  const rows = $$('.menu-item', el);
  let hi = -1, sub = null, subTimer = 0, typed = '', typedAt = 0;
  const layer = Layers.push({
    el, anchor: opts.anchor, returnFocus: opts.returnFocus, onClose: opts.onClose,
    keepOnOutside: opts.keepOnOutside,
    onKey(e) {
      if (sub && Layers.stack.includes(sub)) return false;
      const enabled = rows.filter((r) => !r.hasAttribute('data-disabled'));
      const pos = enabled.indexOf(rows[hi]);
      if (e.key === 'ArrowDown') return highlight(rows.indexOf(enabled[(pos + 1) % enabled.length]), true), true;
      if (e.key === 'ArrowUp') return highlight(rows.indexOf(enabled[(pos - 1 + enabled.length) % enabled.length]), true), true;
      if (e.key === 'Home') return highlight(rows.indexOf(enabled[0]), true), true;
      if (e.key === 'End') return highlight(rows.indexOf(enabled[enabled.length - 1]), true), true;
      if (e.key === 'ArrowRight' && item(hi)?.items) return openSub(true), true;
      if (e.key === 'ArrowLeft' && opts.parent) return Layers.close(layer, 'focus-out'), true;
      if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') return opts.onArrow?.(e.key === 'ArrowLeft' ? -1 : 1) ?? false;
      if (e.key === 'Enter' || e.key === ' ') return activate(hi), true;
      if (e.key === 'Tab') { let root = layer; for (const l of Layers.stack) if (l.el.classList.contains('menu')) { root = l; break; } Layers.close(root, 'focus-out'); return true; }
      if (e.key.length === 1 && /\S/.test(e.key)) {
        const now = performance.now();
        typed = now - typedAt < 500 ? typed + e.key.toLowerCase() : e.key.toLowerCase();
        typedAt = now;
        const match = enabled.find((r) => r.textContent.trim().toLowerCase().startsWith(typed));
        if (match) highlight(rows.indexOf(match), true);
        return true;
      }
      return false;
    },
  });
  const item = (rowIndex) => rowIndex >= 0 ? items[+rows[rowIndex].dataset.index] : null;
  function highlight(i, keyboard) {
    rows.forEach((r, j) => r.toggleAttribute('data-highlighted', j === i));
    hi = i;
    clearTimeout(subTimer);
    if (!keyboard) subTimer = setTimeout(() => (item(i)?.items ? openSub(false) : closeSub()), 100);
  }
  function closeSub() { if (sub) Layers.close(sub); sub = null; }
  function openSub(keyboard) {
    closeSub();
    const it = item(hi);
    sub = openMenu(it.items, rows[hi], { place: { side: 'right', align: 'start', offset: 2 }, parent: layer, onPick: opts.onPick, keyboard });
  }
  function activate(i) {
    const it = item(i);
    if (!it || it.disabled) return;
    if (it.items) return openSub(true);
    let root = layer;
    for (const l of Layers.stack) if (l.el.classList.contains('menu')) { root = l; break; }
    Layers.close(root, 'item-press');
    (it.run || (() => opts.onPick?.(it)))();
  }
  el.addEventListener('pointermove', (e) => {
    const r = e.target.closest('.menu-item');
    if (r && rows.indexOf(r) !== hi) highlight(rows.indexOf(r), false);
  });
  el.addEventListener('pointerleave', () => { if (!sub) highlight(-1); });
  el.addEventListener('click', (e) => { const r = e.target.closest('.menu-item'); if (r) activate(rows.indexOf(r)); });
  if (opts.keyboard) highlight(rows.findIndex((r) => !r.hasAttribute('data-disabled')), true);
  layer.highlight = highlight;
  return layer;
}
function contextMenu(e, items, opts = {}) {
  e.preventDefault();
  for (const l of [...Layers.stack]) Layers.close(l);
  return openMenu(items, { x: e.clientX, y: e.clientY }, { place: { side: 'bottom', align: 'start', offset: 0 }, ...opts });
}

/* Menubar: once open, hovering another trigger switches menus; arrows move between menus. */
function menubar(root, menus) {
  const triggers = $$('[data-menu]', root);
  let open = null;
  function show(t, keyboard) {
    if (open) Layers.close(open.layer);
    t.setAttribute('data-pressed', '');
    const i = triggers.indexOf(t);
    const layer = openMenu(menus[t.dataset.menu](), t, {
      anchor: root, keyboard, returnFocus: t,
      keepOnOutside: (e) => root.contains(e.target),
      onArrow: (d) => { show(triggers[(i + d + triggers.length) % triggers.length], true); return true; },
      onClose: () => { t.removeAttribute('data-pressed'); if (open?.layer === layer) open = null; },
    });
    open = { t, layer };
  }
  triggers.forEach((t) => {
    t.addEventListener('pointerdown', (e) => { e.preventDefault(); if (open?.t === t) Layers.close(open.layer); else show(t, false); });
    t.addEventListener('pointerenter', () => { if (open && open.t !== t) show(t, false); });
    t.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ' || e.key === 'ArrowDown') { e.preventDefault(); show(t, true); } });
  });
  addEventListener('keydown', (e) => { if (e.key === 'F10') { e.preventDefault(); show(triggers[0], true); } });
}

/* ---------- Expression evaluation for numeric entry ("=2*pi", "120*1.5") ---------- */
function evaluate(text, current) {
  let s = text.trim().replace(/^=/, '');
  if (/^[*/+]/.test(s)) s = `${current}${s}`;
  if (!/^[\d\s.+\-*/()epiPI]*$/.test(s) || !s) return NaN;
  try { return Function(`"use strict";const pi=Math.PI,PI=Math.PI;return (${s});`)(); } catch { return NaN; }
}
function format(v, precision) {
  if (!Number.isFinite(v)) return '—';
  const s = v.toFixed(precision);
  return precision > 0 ? s.replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '') : s;
}

/* ---------- NumberField (Base UI NumberField + ScrubArea; Blender field; DialKit slider) ----------
   Click (< 3 px) edits text. Drag scrubs with pointer lock, 2 px per step. One modifier
   rule for keys, scrub and steppers: Alt = smallStep (0.1×), Shift = largeStep (10×),
   Ctrl = snap. Escape cancels a scrub or edit. onChange fires
   live with a reason; onCommit fires once per gesture, which is one undo step. */
function numberField(el, o) {
  const opt = { step: 1, precision: 2, min: -Infinity, max: Infinity, unit: '', ...o };
  const bounded = Number.isFinite(opt.min) && Number.isFinite(opt.max);
  const slider = opt.slider && bounded;
  el.classList.add('field');
  el.tabIndex = 0;
  el.setAttribute('role', slider ? 'slider' : 'spinbutton');
  el.innerHTML = `${slider ? '<span class="fill"></span>' : ''}<span class="mark"></span>`
    + `${opt.addon ? `<span class="part addon ${opt.addonClass || ''}">${opt.addon}</span>` : ''}`
    + `<span class="part value"></span>${opt.unit ? `<span class="part unit">${opt.unit}</span>` : ''}`
    + `${slider ? '' : `<span class="step dec">${icon('left')}</span><span class="step inc">${icon('right')}</span>`}`;
  const valueEl = $('.value', el), fill = $('.fill', el);
  let value = opt.value, startValue = value, editing = null;

  function render() {
    valueEl.textContent = format(value, opt.precision);
    if (fill) fill.style.width = `${clamp((value - opt.min) / (opt.max - opt.min), 0, 1) * 100}%`;
    el.setAttribute('aria-valuenow', value);
    el.toggleAttribute('data-overridden', !!opt.overridden?.());
  }
  function set(v, reason) {
    v = clamp(v, opt.min, opt.max);
    if (opt.integer) v = Math.round(v);
    if (v === value) return;
    value = v;
    render();
    opt.onChange?.(value, reason);
  }
  function commit(reason) { if (value !== startValue) opt.onCommit?.(value, startValue, reason); startValue = value; }
  function stepBy(e, dir) {
    const m = e.shiftKey ? 10 : e.altKey ? 0.1 : 1;
    startValue = value;
    set(+(value + dir * opt.step * m).toFixed(6), 'keyboard');
    commit('keyboard');
  }

  function beginEdit(selectAll = true) {
    if (opt.readOnly?.()) return;
    Tooltip.hide();
    el.setAttribute('data-editing', '');
    const input = h(`<input class="part value" spellcheck="false">`);
    input.value = format(value, Math.max(opt.precision, 3));
    valueEl.replaceWith(input);
    editing = input;
    input.focus();
    if (selectAll) input.select();
    input.addEventListener('keydown', (e) => {
      e.stopPropagation();
      if (e.key === 'Enter') { e.preventDefault(); endEdit(true); }
      if (e.key === 'Escape') { e.preventDefault(); endEdit(false); el.focus(); }
      if (e.key === 'Tab') {
        e.preventDefault();
        if (!endEdit(true)) return;
        const fields = $$('.field[tabindex]').filter((f) => f.offsetParent && f._field);
        const next = fields[(fields.indexOf(el) + (e.shiftKey ? -1 : 1) + fields.length) % fields.length];
        next?._field.beginEdit();
      }
    });
    input.addEventListener('blur', () => { if (editing === input) endEdit(true); });
  }
  function endEdit(accept) {
    const input = editing;
    if (!input) return true;
    if (accept) {
      const v = evaluate(input.value, value);
      if (!Number.isFinite(v)) {
        el.setAttribute('data-invalid', '');
        opt.onInvalid?.(input.value);
        setTimeout(() => el.removeAttribute('data-invalid'), 600);
        input.select();
        return false;
      }
      startValue = value;
      set(v, 'input-change');
      commit('input-commit');
    }
    editing = null;
    input.replaceWith(valueEl);
    el.removeAttribute('data-editing');
    render();
    return true;
  }

  let press = null;
  el.addEventListener('pointerdown', (e) => {
    if (e.button !== 0 || editing || opt.readOnly?.()) return;
    const stepEl = e.target.closest('.step');
    e.preventDefault();
    el.focus();
    if (stepEl) {
      const dir = stepEl.classList.contains('inc') ? 1 : -1;
      const first = value;
      const tick = () => { set(+(value + dir * opt.step * (e.shiftKey ? 10 : e.altKey ? 0.1 : 1)).toFixed(6), dir > 0 ? 'increment-press' : 'decrement-press'); };
      tick();
      let repeat = setTimeout(function again() { tick(); repeat = setTimeout(again, 60); }, 400);
      const stop = () => { clearTimeout(repeat); removeEventListener('pointerup', stop); startValue = first; commit('increment-press'); };
      addEventListener('pointerup', stop);
      return;
    }
    press = { x: e.clientX, dx: 0, scrub: false, id: e.pointerId };
    startValue = value;
    el.setPointerCapture(e.pointerId);
  });
  el.addEventListener('pointermove', (e) => {
    if (!press) return;
    const moved = document.pointerLockElement === el ? e.movementX : e.clientX - press.x - press.dx;
    press.dx += moved;
    if (!press.scrub && Math.abs(press.dx) >= 3) {
      press.scrub = true;
      el.setAttribute('data-scrubbing', '');
      el.requestPointerLock?.()?.catch?.(() => {});
      Tooltip.hide();
    }
    if (!press.scrub) return;
    const width = el.getBoundingClientRect().width;
    const perPx = slider ? (opt.max - opt.min) / width : opt.step / (opt.pixelsPerStep || 2);
    const m = e.altKey ? 0.1 : e.shiftKey && !slider ? 10 : 1;
    press.v = (press.v ?? startValue) + moved * perPx * m;
    let v = press.v;
    if (e.ctrlKey || e.metaKey) { const snap = slider ? (opt.max - opt.min) / 10 : opt.step * 10; v = Math.round(v / snap) * snap; }
    set(+v.toFixed(6), 'scrub');
  });
  const endPress = (cancel) => {
    if (!press) return;
    const wasScrub = press.scrub;
    press = null;
    el.removeAttribute('data-scrubbing');
    if (document.pointerLockElement === el) document.exitPointerLock();
    if (!wasScrub) { beginEdit(); return; }
    if (cancel) { set(startValue, 'cancel'); return; }
    commit('scrub-end');
  };
  el.addEventListener('pointerup', () => endPress(false));
  el.addEventListener('lostpointercapture', () => { if (press?.scrub) endPress(false); });
  el.addEventListener('contextmenu', (e) => {
    if (press?.scrub) { e.preventDefault(); endPress(true); return; }
    if (opt.menu) contextMenu(e, opt.menu());
  });
  el.addEventListener('keydown', (e) => {
    if (editing) return;
    if (press?.scrub && e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); endPress(true); return; }
    const k = e.key;
    if (k === 'ArrowUp' || k === 'ArrowRight') { e.preventDefault(); stepBy(e, 1); }
    else if (k === 'ArrowDown' || k === 'ArrowLeft') { e.preventDefault(); stepBy(e, -1); }
    else if (k === 'Home' && bounded) { e.preventDefault(); startValue = value; set(opt.min, 'keyboard'); commit('keyboard'); }
    else if (k === 'End' && bounded) { e.preventDefault(); startValue = value; set(opt.max, 'keyboard'); commit('keyboard'); }
    else if (k === 'Enter' || (/^[\d.\-=]$/.test(k) && !e.metaKey && !e.ctrlKey)) {
      e.preventDefault();
      beginEdit(k === 'Enter');
      if (k !== 'Enter') editing.value = k;
    } else if ((k === 'Backspace' || k === 'Delete') && opt.reset) { e.preventDefault(); opt.reset(); }
  });
  const api = {
    el, beginEdit,
    get value() { return value; },
    setValue(v) { value = v; startValue = v; if (!editing) render(); },
    refresh: render,
  };
  el._field = api;
  render();
  return api;
}

/* Hover copy/paste (Blender): Mod+C / Mod+V over any value copies or pastes it. */
let hoverField = null;
document.addEventListener('pointerover', (e) => { hoverField = e.target.closest('.field')?._field || null; });
addEventListener('keydown', (e) => {
  if (!hoverField || !(e.metaKey || e.ctrlKey) || document.activeElement?.tagName === 'INPUT') return;
  if (e.key === 'c') { e.preventDefault(); navigator.clipboard?.writeText(String(hoverField.value)); Kit.report?.(`Copied ${format(hoverField.value, 3)}`); }
  if (e.key === 'v') {
    e.preventDefault();
    navigator.clipboard?.readText().then((t) => { const v = parseFloat(t); if (Number.isFinite(v)) hoverField.el.dispatchEvent(new CustomEvent('paste-value', { detail: v })); });
  }
});

/* ---------- Select (Base UI Select: popup aligns the selected item over the trigger) ---------- */
function select(el, o) {
  el.classList.add('field', 'select');
  el.tabIndex = 0;
  el.setAttribute('role', 'combobox');
  const render = () => {
    const cur = o.options.find((x) => x.value === o.value());
    el.innerHTML = `${cur?.icon ? icon(cur.icon) : ''}<span class="part value">${cur ? cur.label : '—'}</span>${icon('down', 'caret')}`;
  };
  function open(keyboard) {
    if (o.readOnly?.()) return;
    el.setAttribute('data-open', '');
    const items = o.options.map((x) => ({ label: x.label, icon: x.icon, checked: x.value === o.value(), run: () => o.onChange(x.value) }));
    const layer = openMenu(items, el, { keyboard, returnFocus: el, minWidth: el.getBoundingClientRect().width, anchor: el, onClose: () => { el.removeAttribute('data-open'); render(); } });
    const cur = items.findIndex((x) => x.checked);
    const row = $$('.menu-item', layer.el)[cur];
    if (row) {
      layer.highlight(cur, true);
      const r = el.getBoundingClientRect(), rr = row.getBoundingClientRect(), pr = layer.el.getBoundingClientRect();
      layer.el.style.top = `${clamp(r.top - (rr.top - pr.top), 4, innerHeight - pr.height - 4)}px`;
      layer.el.style.left = `${r.left - 4}px`;
    }
  }
  el.addEventListener('pointerdown', (e) => { if (e.button === 0) { e.preventDefault(); el.focus(); if (!el.hasAttribute('data-open')) open(false); } });
  el.addEventListener('keydown', (e) => { if (['Enter', ' ', 'ArrowDown', 'ArrowUp'].includes(e.key)) { e.preventDefault(); open(true); } });
  render();
  return { el, refresh: render };
}

/* ---------- Toggle group (Base UI ToggleGroup, single value) ---------- */
function toggleGroup(el, o) {
  el.classList.add('segmented');
  el.setAttribute('role', 'radiogroup');
  const render = () => {
    el.innerHTML = o.options.map((x) => `<button class="seg" role="radio" tabindex="${x.value === o.value() ? 0 : -1}" ${x.value === o.value() ? 'data-pressed' : ''} data-value="${x.value}" ${x.tip ? `data-tip="${x.tip}"` : ''}>${x.icon ? icon(x.icon) : ''}${x.label ? `<span>${x.label}</span>` : ''}</button>`).join('');
  };
  el.addEventListener('click', (e) => { const b = e.target.closest('.seg'); if (b && !o.readOnly?.()) { o.onChange(isNaN(+b.dataset.value) ? b.dataset.value : +b.dataset.value); render(); } });
  el.addEventListener('keydown', (e) => {
    const d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
    if (!d) return;
    e.preventDefault();
    const i = o.options.findIndex((x) => x.value === o.value());
    o.onChange(o.options[(i + d + o.options.length) % o.options.length].value);
    render();
    $('[data-pressed]', el).focus();
  });
  render();
  return { el, refresh: render };
}

/* ---------- Checkbox ---------- */
function checkbox(el, o) {
  el.classList.add('check');
  el.tabIndex = 0;
  el.setAttribute('role', 'checkbox');
  const render = () => {
    el.innerHTML = `<span class="box">${icon('check')}</span>${o.label ? `<span>${o.label}</span>` : ''}`;
    el.toggleAttribute('data-checked', !!o.value());
    el.setAttribute('aria-checked', !!o.value());
  };
  const toggle = () => { if (!o.readOnly?.()) { o.onChange(!o.value()); render(); } };
  el.addEventListener('click', toggle);
  el.addEventListener('keydown', (e) => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); toggle(); } });
  render();
  return { el, refresh: render };
}

/* ---------- Color field + picker popover ---------- */
const hex = (c) => '#' + [c.r, c.g, c.b].map((v) => Math.round(v).toString(16).padStart(2, '0')).join('');
function hsvToRgb(hh, s, v) {
  const f = (n) => { const k = (n + hh / 60) % 6; return v - v * s * Math.max(0, Math.min(k, 4 - k, 1)); };
  return { r: f(5) * 255, g: f(3) * 255, b: f(1) * 255 };
}
function rgbToHsv({ r, g, b }) {
  r /= 255; g /= 255; b /= 255;
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b), d = mx - mn;
  let hh = 0;
  if (d) hh = mx === r ? ((g - b) / d) % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
  return { h: (hh * 60 + 360) % 360, s: mx ? d / mx : 0, v: mx };
}
function colorField(el, o) {
  el.classList.add('field', 'color');
  el.tabIndex = 0;
  const render = () => {
    const c = o.value();
    el.innerHTML = c.a === 0
      ? '<span class="mark"></span><span class="swatch none"></span>'
      : `<span class="mark"></span><span class="swatch" style="background:${hex(c)}"></span>${c.a < 1 ? `<span class="swatch-alpha" style="background:${hex(c)};opacity:${c.a}"></span>` : ''}`;
    el.toggleAttribute('data-overridden', !!o.overridden?.());
  };
  function open() {
    if (o.readOnly?.()) return;
    const start = { ...o.value() };
    let hsv = rgbToHsv(start);
    const pop = h(`<div class="popup picker" role="dialog">
      <div class="sv"><canvas width="216" height="150"></canvas><span class="puck"></span></div>
      <div class="hue"><canvas width="216" height="12"></canvas><span class="puck"></span></div>
      <div class="row" style="margin-top:8px"><div class="field text left" style="flex:1"><span class="part addon">Hex</span><input class="part value" spellcheck="false"></div></div>
    </div>`);
    mount(pop);
    place(pop, el, { side: 'bottom', align: 'end', offset: 4 });
    const [sv, hue] = $$('canvas', pop), [svPuck, huePuck] = $$('.puck', pop), hexIn = $('input', pop);
    function paint() {
      const g = sv.getContext('2d'), base = hsvToRgb(hsv.h, 1, 1);
      g.fillStyle = hex(base); g.fillRect(0, 0, sv.width, sv.height);
      const wh = g.createLinearGradient(0, 0, sv.width, 0); wh.addColorStop(0, '#fff'); wh.addColorStop(1, '#fff0'); g.fillStyle = wh; g.fillRect(0, 0, sv.width, sv.height);
      const bk = g.createLinearGradient(0, 0, 0, sv.height); bk.addColorStop(0, '#0000'); bk.addColorStop(1, '#000'); g.fillStyle = bk; g.fillRect(0, 0, sv.width, sv.height);
      const hg = hue.getContext('2d'), grad = hg.createLinearGradient(0, 0, hue.width, 0);
      for (let i = 0; i <= 6; i++) grad.addColorStop(i / 6, hex(hsvToRgb(i * 60, 1, 1)));
      hg.fillStyle = grad; hg.fillRect(0, 0, hue.width, hue.height);
      svPuck.style.left = `${hsv.s * 100}%`; svPuck.style.top = `${(1 - hsv.v) * 100}%`;
      huePuck.style.left = `${hsv.h / 360 * 100}%`;
      if (document.activeElement !== hexIn) hexIn.value = hex(hsvToRgb(hsv.h, hsv.s, hsv.v)).slice(1).toUpperCase();
    }
    const emit = (reason) => { o.onChange({ ...hsvToRgb(hsv.h, hsv.s, hsv.v), a: o.value().a }, reason); render(); paint(); };
    const drag = (canvas, fn) => canvas.parentElement.addEventListener('pointerdown', (e) => {
      const move = (ev) => { const r = canvas.getBoundingClientRect(); fn(clamp((ev.clientX - r.left) / r.width, 0, 1), clamp((ev.clientY - r.top) / r.height, 0, 1)); emit('drag'); };
      move(e);
      const up = () => { removeEventListener('pointermove', move); removeEventListener('pointerup', up); };
      addEventListener('pointermove', move); addEventListener('pointerup', up);
    });
    drag(sv, (x, y) => { hsv.s = x; hsv.v = 1 - y; });
    drag(hue, (x) => { hsv.h = x * 360; });
    hexIn.addEventListener('keydown', (e) => {
      e.stopPropagation();
      if (e.key === 'Escape') { o.onChange(start, 'cancel'); render(); Layers.close(layer, 'escape-key'); el.focus(); return; }
      if (e.key !== 'Enter') return;
      const m = /^#?([0-9a-f]{6})$/i.exec(hexIn.value.trim());
      if (!m) return;
      const n = parseInt(m[1], 16);
      hsv = rgbToHsv({ r: n >> 16, g: (n >> 8) & 255, b: n & 255 });
      emit('input-commit');
    });
    const layer = Layers.push({ el: pop, anchor: el, returnFocus: el, onClose: (reason) => { if (reason === 'escape-key') { o.onChange(start, 'cancel'); render(); } else o.onCommit?.(start); } });
    paint();
  }
  el.addEventListener('click', open);
  el.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); open(); } });
  el.addEventListener('contextmenu', (e) => { if (o.menu) contextMenu(e, o.menu()); });
  render();
  return { el, refresh: render };
}

/* ---------- Collapsible panel (Blender panel; Mod+click opens only this one) ---------- */
function panel(el, o) {
  el.classList.add('panel');
  const head = $('.panel-head', el);
  head.tabIndex = 0;
  const set = (open) => { el.toggleAttribute('data-open', open); head.setAttribute('aria-expanded', open); o?.onToggle?.(open); };
  head.addEventListener('click', (e) => {
    if (e.target.closest('[data-stop]')) return;
    const open = !el.hasAttribute('data-open');
    if (e.metaKey || e.ctrlKey) {
      $$('.panel', el.parentElement).forEach((p) => p !== el && p._panel?.set(false));
      set(true);
      return;
    }
    set(open);
  });
  head.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); set(!el.hasAttribute('data-open')); } });
  el._panel = { set };
  return el._panel;
}

/* ---------- command-score (cmdk): ported ranking ---------- */
function commandScore(string, abbreviation, aliases = []) {
  string = (aliases.length ? `${string} ${aliases.join(' ')}` : string);
  const lower = string.toLowerCase().replace(/[\s-]+/g, ' ');
  const abbr = abbreviation.toLowerCase().replace(/[\s-]+/g, ' ');
  const memo = {};
  const GAP = /[\\/_+.#"@[({&]/, SPACE = /[\s-]/;
  function score(si, ai) {
    const key = `${si},${ai}`;
    if (memo[key] !== undefined) return memo[key];
    if (ai === abbr.length) return si === lower.length ? 1 : 0.99;
    let best = 0, index = lower.indexOf(abbr[ai], si);
    while (index >= 0) {
      let s = score(index + 1, ai + 1);
      if (s > best) {
        if (index === si) s *= 1;
        else if (SPACE.test(lower[index - 1])) { s *= 0.9; const breaks = lower.slice(si, index - 1).match(/[\s-]/g); if (breaks && si > 0) s *= 0.999 ** breaks.length; }
        else if (GAP.test(lower[index - 1])) { s *= 0.8; const breaks = lower.slice(si, index - 1).match(GAP); if (breaks && si > 0) s *= 0.999 ** breaks.length; }
        else { s *= 0.17; if (si > 0) s *= 0.999 ** (index - si); }
        if (string[index] !== abbreviation[ai]) s *= 0.9999;
      }
      if (s < 0.1 && lower[index - 1] === abbr[ai + 1] && lower[index - 1] !== abbr[ai]) {
        const t = score(index + 1, ai + 2) * 0.1;
        if (t > s) s = t;
      }
      if (s > best) best = s;
      index = lower.indexOf(abbr[ai], index + 1);
    }
    return (memo[key] = best);
  }
  return score(0, 0);
}
function highlightMatch(label, q) {
  if (!q) return label;
  const l = label.toLowerCase(), qq = q.toLowerCase();
  let out = '', j = 0;
  for (let i = 0; i < label.length; i++) {
    if (j < qq.length && l[i] === qq[j]) { out += `<b>${label[i]}</b>`; j++; } else out += label[i];
  }
  return j === qq.length ? out : label;
}

/* ---------- Command palette (cmdk + Dialog). Focus stays in the input. ---------- */
function command(o) {
  const backdrop = h('<div class="command-backdrop"></div>');
  const el = h(`<div class="command" role="dialog" aria-label="Command">
    <div class="cmd-input">${icon('search')}<span class="pages"></span><input placeholder="${o.placeholder || 'Search'}" spellcheck="false" role="combobox" aria-expanded="true"></div>
    <div class="cmd-list scroll" role="listbox"></div>
    <div class="cmd-footer"><span class="row"><span class="kbd-key">↑</span><span class="kbd-key">↓</span>Navigate</span><span class="row"><span class="kbd-key">↵</span><span class="action-label">Open</span></span><span class="spacer"></span><span class="row"><span class="kbd-key">esc</span><span class="esc-label">Close</span></span></div>
  </div>`);
  const input = $('input', el), list = $('.cmd-list', el), pagesEl = $('.pages', el);
  const pages = [];
  let selected = null, shown = [];
  mount(backdrop); mount(el);
  const returnFocus = document.activeElement;
  const layer = Layers.push({
    el, returnFocus, onClose: () => { unmount(backdrop); o.onClose?.(); },
    onKey(e) {
      if (e.key === 'Escape' && pages.length) { pop(); return true; }
      const i = shown.indexOf(selected);
      const vim = e.ctrlKey && (e.key === 'n' || e.key === 'j') ? 1 : e.ctrlKey && (e.key === 'p' || e.key === 'k') ? -1 : 0;
      if (e.key === 'ArrowDown' || vim === 1) return select(shown[Math.min(i + 1, shown.length - 1)]), true;
      if (e.key === 'ArrowUp' || vim === -1) return select(shown[Math.max(i - 1, 0)]), true;
      if (e.key === 'Home' && e.metaKey) return select(shown[0]), true;
      if (e.key === 'End' && e.metaKey) return select(shown[shown.length - 1]), true;
      if (e.key === 'Enter') return run(selected), true;
      if (e.key === 'Backspace' && !input.value && pages.length) return pop(), true;
      return false;
    },
  });
  // Escape closes pages first; intercept before the layer's default close.
  el.addEventListener('keydown', (e) => { if (e.key === 'Escape' && pages.length) { e.stopPropagation(); } });
  const items = () => (pages.length ? pages[pages.length - 1].items() : o.items());
  function render() {
    const q = input.value.trim();
    const all = items();
    let rows = all.map((it) => ({ it, s: q ? commandScore(it.label, q, it.keywords || []) : 1 })).filter((r) => r.s > (q.length > 1 ? 0.02 : 0));
    if (q) rows.sort((a, b) => b.s - a.s);
    const groups = [];
    for (const r of rows) {
      let g = groups.find((x) => x.name === (r.it.group || ''));
      if (!g) groups.push(g = { name: r.it.group || '', rows: [], best: r.s });
      if (!q || g.rows.length < (o.groupLimit || 8)) g.rows.push(r);
    }
    shown = groups.flatMap((g) => g.rows.map((r) => r.it));
    list.innerHTML = shown.length ? groups.map((g) => `${g.name ? `<div class="cmd-group">${g.name}</div>` : ''}${g.rows.map((r) =>
      `<div class="cmd-item" role="option" data-id="${r.it.id}">${icon(r.it.icon || 'file')}<span class="label">${highlightMatch(r.it.label, q)}</span>${r.it.meta ? `<span class="meta">${r.it.meta}</span>` : ''}${r.it.kbd ? `<span class="kbd">${kbd(r.it.kbd)}</span>` : ''}${r.it.page ? icon('right', 'sm') : ''}</div>`).join('')}`).join('')
      : `<div class="cmd-empty">No results for “${q}”</div>`;
    select(shown.includes(selected) && !q ? selected : shown[0]);
    pagesEl.innerHTML = pages.map((p) => `<span class="cmd-page">${p.label}</span>`).join('');
  }
  function select(it) {
    selected = it;
    $$('.cmd-item', list).forEach((r) => {
      const on = it && r.dataset.id === String(it.id);
      r.toggleAttribute('data-selected', on);
      if (on) r.scrollIntoView({ block: 'nearest' });
    });
    $('.action-label', el).textContent = it?.page ? 'Open' : it?.action || 'Open';
  }
  function run(it) {
    if (!it) return;
    if (it.page) { pages.push({ label: it.page, items: it.items }); input.value = ''; render(); return; }
    Layers.close(layer, 'item-press');
    it.run?.();
  }
  function pop() { pages.pop(); input.value = ''; render(); }
  list.addEventListener('pointermove', (e) => {
    const r = e.target.closest('.cmd-item');
    if (r && r.dataset.id !== String(selected?.id)) select(shown.find((x) => String(x.id) === r.dataset.id));
  });
  list.addEventListener('click', (e) => { const r = e.target.closest('.cmd-item'); if (r) run(shown.find((x) => String(x.id) === r.dataset.id)); });
  input.addEventListener('input', render);
  input.addEventListener('keydown', (e) => e.stopPropagation());
  input.addEventListener('keydown', (e) => { if (layer.onKey(e)) e.preventDefault(); if (e.key === 'Escape') { if (pages.length) pop(); else { Layers.close(layer, 'escape-key'); returnFocus?.focus?.(); } } });
  if (o.page) pages.push(o.page);
  render();
  input.focus();
  return layer;
}

/* ---------- Splitter (shadcn Resizable: 1 px line, 5 px hit area) ---------- */
function splitter(el, o) {
  el.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    el.setAttribute('data-dragging', '');
    const start = o.get(), x0 = e.clientX, y0 = e.clientY;
    const move = (ev) => o.set(start + (o.axis === 'x' ? ev.clientX - x0 : ev.clientY - y0) * (o.invert ? -1 : 1));
    const up = () => { el.removeAttribute('data-dragging'); el.removeEventListener('pointermove', move); el.removeEventListener('pointerup', up); o.done?.(); };
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', up);
  });
  el.addEventListener('dblclick', () => o.reset?.());
}

window.Kit = { $, $$, h, clamp, kbd, mod, isMac, mount, unmount, place, Tooltip, Layers, openMenu, contextMenu, menubar, numberField, select, toggleGroup, checkbox, colorField, panel, command, commandScore, splitter, format, evaluate, hex };
})();
