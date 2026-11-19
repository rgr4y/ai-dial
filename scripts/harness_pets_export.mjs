#!/usr/bin/env node
// Decode the Harness dial's pets from the generated firmware source and export them for docs/harness-pets.
//
//     node scripts/harness_pets_export.mjs [path/to/pets.c] [out_dir]
//
// Dependencies come from bridge/ (run `npm install` there once).
//
// Source of truth: devices/harness-device/firmware/main/ui/habitat/pets.c (written by scripts/gen_pets.py),
// i.e. exactly the bytes the firmware links, not the PNGs gen_pets.py starts from.
//
// Format decoded (pets.h / terminal.h ht_cell_frame_t):
//   - A frame is cols x rows cells, `cell` px each, one byte per cell = palette index, 0 = transparent.
//   - Palettes are uint16 RGB565 in PANEL order (byte-swapped); entry 0 unused.
//   - Packed when row_at != NULL: row r starts at cells[row_at[r]] and is (skip, count) byte pairs, each followed
//     by `count` indices, until `cols` are covered (transparent runs cost two bytes; runs <= 255).
//   - Bar fills / shape colours are native RGB565; wave colours are 8-bit RGB triples.
// dedupe_arrays() in gen_pets.py aliases identical arrays across pets, so every name resolves through one global
// symbol table.
//
// Writes out_dir/assets/<frames array>/NN.png (cell-expanded, RGB565 widened to 8 bit, alpha 0/255),
// out_dir/assets/<pet>/zZ/NN.png (the small pet pre-rendered with terminal.c sprite_raster's zoom box filter,
// Z = 4..8 eighths) and out_dir/data.js (window.HT = {...}: every pet, scene, loop, overlay, bars, waves, shapes).
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const { PNG } = createRequire(join(ROOT, 'bridge/package.json'))('pngjs');
const SRC = process.argv[2] ?? join(ROOT, 'vendor/openharness/devices/harness-device/firmware/main/ui/habitat/pets.c');
const OUT = process.argv[3] ?? join(ROOT, 'docs/harness-pets');

// Split a C initializer body on top-level commas.
function splitTop(body) {
  const out = [];
  let depth = 0, cur = '';
  for (const ch of body) {
    if (ch === '{') depth++;
    else if (ch === '}') depth--;
    if (ch === ',' && depth === 0) {
      out.push(cur.trim());
      cur = '';
    } else cur += ch;
  }
  if (cur.trim()) out.push(cur.trim());
  return out;
}

function val(tok) {
  tok = tok.trim();
  if (tok.startsWith('{')) return splitTop(tok.slice(1, -1)).map(val);
  if (tok.startsWith('"')) return tok.replace(/^"+|"+$/g, '');
  if (tok === 'NULL') return null;
  if (/^-?\d+$/.test(tok)) return parseInt(tok, 10);
  if (/^-?[\d.]+f$/.test(tok)) return parseFloat(tok);  // ponytail: a whole float (1.0f) prints as 1, not 1.0
  return tok.replace(/^&+/, '');  // a symbol reference
}

// name -> [type, value, line] for every `[static] const T name[...] = {...};`.
function parse(text) {
  const syms = {};
  const rx = /^(?:static )?const (\w+) (\w+)(?:\[[^\]]*\])* = (\{.*\});$/;
  text.split(/\r?\n/).forEach((line, i) => {
    const m = rx.exec(line);
    if (m) syms[m[2]] = [m[1], val(m[3]), i + 1];
  });
  return syms;
}

const rgb565ToRgb = (v) => {
  const r = v >> 11, g = (v >> 5) & 63, b = v & 31;
  return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)];
};
const panel = (v) => ((v << 8) | (v >> 8)) & 0xffff;
const assert = (ok, ...why) => { if (!ok) throw new Error(`assertion failed: ${JSON.stringify(why)}`); };

// Grid of palette indices (rows of cols) for an ht_cell_frame_t initializer.
function unpack(frame, syms) {
  const [cols, rows, , , cells, rowAt] = frame;
  const data = syms[cells][1];
  if (rowAt === null) {
    assert(data.length === cols * rows, cells, data.length, cols, rows);
    return Array.from({ length: rows }, (_, r) => data.slice(r * cols, (r + 1) * cols));
  }
  const starts = syms[rowAt][1];
  const grid = [];
  for (let r = 0; r < rows; r++) {
    let p = starts[r];
    const row = [];
    while (row.length < cols) {
      const skip = data[p], n = data[p + 1];
      for (let k = 0; k < skip; k++) row.push(0);
      row.push(...data.slice(p + 2, p + 2 + n));
      p += 2 + n;
    }
    assert(row.length === cols, cells, r, row.length, cols);
    grid.push(row);
  }
  return grid;
}

function savePng(png, file) {
  writeFileSync(file, PNG.sync.write(png, { colorType: 6 }));
}

function frameImage(frame, syms) {
  const [cols, rows, cell, pal] = frame;
  const palette = syms[pal][1].map((c) => rgb565ToRgb(panel(c)));
  const grid = unpack(frame, syms);
  const png = new PNG({ width: cols * cell, height: rows * cell });
  png.data.fill(0);
  for (let y = 0; y < rows * cell; y++)
    for (let x = 0; x < cols * cell; x++) {
      const i = grid[Math.floor(y / cell)][Math.floor(x / cell)];
      if (i) png.data.set([...palette[i], 255], (y * png.width + x) * 4);
    }
  return png;
}

// terminal.c sprite_raster, zoomed branch: each glass px the overlap-weighted mean of frame px over black,
// left untouched (transparent here) when less than a quarter covered. Done on the RGB565 channels as there.
function zoomImage(frame, syms, z) {
  const [cols, rows, cell, pal] = frame;
  const palette = syms[pal][1].map(panel);
  const grid = unpack(frame, syms);
  const sw = cols * cell, sh = rows * cell;
  const W = Math.floor((sw * z + 7) / 8), H = Math.floor((sh * z + 7) / 8);
  const png = new PNG({ width: W, height: H });
  png.data.fill(0);
  for (let y = 0; y < H; y++) {
    const v0 = y * 8, v1 = y * 8 + 8;
    for (let x = 0; x < W; x++) {
      const u0 = x * 8, u1 = x * 8 + 8;
      let rr = 0, gg = 0, bb = 0, cover = 0;
      for (let sy = Math.floor(v0 / z); sy * z < v1 && sy < sh; sy++) {
        const wy = Math.min(v1, (sy + 1) * z) - Math.max(v0, sy * z);
        for (let sx = Math.floor(u0 / z); sx * z < u1 && sx < sw; sx++) {
          const i = grid[Math.floor(sy / cell)][Math.floor(sx / cell)];
          if (!i) continue;
          const w = wy * (Math.min(u1, (sx + 1) * z) - Math.max(u0, sx * z));
          const c = palette[i];
          rr += (c >> 11) * w; gg += ((c >> 5) & 63) * w; bb += (c & 31) * w; cover += w;
        }
      }
      if (cover * 4 < 64) continue;
      const c = (Math.floor((rr + 32) / 64) << 11) | (Math.floor((gg + 32) / 64) << 5) | Math.floor((bb + 32) / 64);
      png.data.set([...rgb565ToRgb(c), 255], (y * W + x) * 4);
    }
  }
  return png;
}

const pad2 = (k) => String(k).padStart(2, '0');

function main() {
  const syms = parse(readFileSync(SRC, 'utf8'));
  const assets = join(OUT, 'assets');
  const exported = {};

  // Export a frames array once; returns its asset dir, sizes and source line.
  function frames(name) {
    if (exported[name]) return exported[name];
    const [typ, arr, line] = syms[name];
    assert(typ === 'ht_cell_frame_t', name, typ);
    const d = join(assets, name);
    mkdirSync(d, { recursive: true });
    const sizes = [];
    arr.forEach((fr, k) => {
      const im = frameImage(fr, syms);
      savePng(im, join(d, `${pad2(k)}.png`));
      sizes.push([im.width, im.height]);
      console.log(`  ${name}[${k}] ${im.width}x${im.height}`);
    });
    return (exported[name] = { dir: `assets/${name}`, n: arr.length, size: sizes, line });
  }

  function overlay(name) {
    if (!name) return null;
    const [, [fr, loop, at], line] = syms[name];
    return { frames: frames(fr), loop: syms[loop][1], at: syms[at][1], line };
  }

  function scene(name) {
    if (!name) return null;
    const [, v, line] = syms[name];
    const [w, h, fr, loop, steps, stepMs, ov, dx, dy, bars, waves, stepDy, countAt, shapes] = v;
    console.log(`scene ${name}: ${steps} steps x ${stepMs} ms`);
    const s = {
      name, line, w, h, steps, step_ms: stepMs, dx, dy,
      frames: fr ? frames(fr) : null, loop: loop ? syms[loop][1] : null,
      overlay: overlay(ov), step_dy: stepDy ? syms[stepDy][1] : null,
      count_at: countAt ? syms[countAt][1] : null,
    };
    if (s.loop !== null) {
      assert(s.loop.length % steps === 0, name);
      s.levels = s.loop.length / steps;
    }
    if (bars) {
      const [x, cy, bw, radius, minH, swing, fill, period, phase] = syms[bars][1];
      s.bars = { x, cy, w: bw, radius, min_h: minH, swing, fill: fill.map(rgb565ToRgb), period_ms: period, phase,
                 line: syms[bars][2] };
    }
    if (waves) {
      const [cx16, cy16, rf, rn, w16, half, count, rgb, period, gap] = syms[waves][1];
      s.waves = { cx16, cy16, r_far16: rf, r_near16: rn, w16, half_deg: half, count, rgb, period_ms: period,
                  gap16: gap, line: syms[waves][2] };
    }
    if (shapes) {
      const [count, at] = syms[shapes][1];
      s.shapes = { count, line: syms[shapes][2],
                   at: syms[at][1].map(([cx, cy, r, sw, mid, half, c]) => [cx, cy, r, sw, mid, half, rgb565ToRgb(c)]) };
      assert(s.shapes.at.length === count * steps, name);
    }
    return s;
  }

  const pets = [];
  const [, table, tableLine] = syms.ht_pets;
  for (const row of table) {
    const [engine, w, h, , loops, stepMs, work, listen, send, cells, alert, steps, relax, alt] = row;
    console.log(`pet ${engine}`);
    const small = frames(cells);
    const zoom = {};
    for (const z of [4, 5, 6, 7, 8]) {
      const d = join(assets, engine, `z${z}`);
      mkdirSync(d, { recursive: true });
      syms[cells][1].forEach((fr, k) =>
        savePng(z < 8 ? zoomImage(fr, syms, z) : frameImage(fr, syms), join(d, `${pad2(k)}.png`)));
      zoom[z] = `assets/${engine}/z${z}`;
    }
    // first/last ink row of each small frame (focus.c cell_ink_rows), for centring by ink
    const ink = syms[cells][1].map((fr) => {
      const r = unpack(fr, syms).flatMap((row_, i) => (row_.some(Boolean) ? [i] : []));
      return r.length ? [r[0], r.at(-1) + 1] : [0, 0];
    });
    pets.push({
      engine, w, h, steps: steps || 24,
      loops: syms[loops][1], loops_line: syms[loops][2],
      step_ms: syms[stepMs][1], step_ms_line: syms[stepMs][2],
      small, zoom, ink,
      working: scene(work), working_alt: scene(alt), listening: scene(listen),
      sending: scene(send), alert: scene(alert), relaxing: scene(relax),
    });
  }
  const data = { source: 'main/ui/habitat/pets.c', table_line: tableLine, pets };
  writeFileSync(join(OUT, 'data.js'),
    `// Generated by scripts/harness_pets_export.mjs from pets.c. Do not edit.\nwindow.HT = ${JSON.stringify(data)};\n`);
  console.log(`wrote ${join(OUT, 'data.js')}`);
}

main();
