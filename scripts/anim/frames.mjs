// LVGL frame helpers shared by the pack generator and the scene builders. No side effects on import.
// LVGL RLE (libs/rle/lv_rle.c) on 2-byte blocks, as lv_bin_decoder uses for RGB565A8. A control byte with bit 7 set
// is followed by (ctrl & 0x7f) literal blocks; otherwise one block repeats ctrl times. The blob is padded to whole
// blocks and the last block always goes out as a literal, so a decoder that truncates at the output size never
// drops real bytes.
const BLK = 2;
export function rle(raw) {
  const buf = Buffer.concat([raw, Buffer.alloc(raw.length % BLK)]);
  const n = buf.length / BLK, last = n - 1, out = [];
  const same = (a, b) => buf.compare(buf, b * BLK, b * BLK + BLK, a * BLK, a * BLK + BLK) === 0;
  let i = 0;
  while (i < last) {
    let r = 1;
    while (i + r < last && r < 127 && same(i, i + r)) r++;
    if (r >= 2) {
      out.push(Buffer.from([r]), buf.subarray(i * BLK, i * BLK + BLK));
      i += r;
      continue;
    }
    let l = 1;
    while (i + l < last && l < 127 && !(i + l + 1 < last && same(i + l, i + l + 1))) l++;
    out.push(Buffer.from([0x80 | l]), buf.subarray(i * BLK, (i + l) * BLK));
    i += l;
  }
  out.push(Buffer.from([0x81]), buf.subarray(last * BLK));
  return Buffer.concat(out);
}
// Mirrors LVGL lv_rle_decompress, for the round-trip check.
export function unrle(src, outLen) {
  const out = Buffer.alloc(outLen + BLK);
  let r = 0, w = 0;
  while (r < src.length && w < outLen) {
    const c = src[r++];
    if (c & 0x80) {
      const b = (c & 0x7f) * BLK;
      src.copy(out, w, r, r + b);
      r += b; w += b;
    } else {
      for (let k = 0; k < c; k++) src.copy(out, w + k * BLK, r, r + BLK);
      r += BLK; w += c * BLK;
    }
  }
  return out.subarray(0, outLen);
}

// lv_image_header_t (magic 0x19, cf RGB565A8 0x14, flags COMPRESSED 0x08, w, h, stride) + lv_image_compressed_t
// header (method RLE = 1, compressed size, decompressed size) + RLE body.
export function frameFile(w, h, raw) {
  const body = rle(raw);
  if (!unrle(body, raw.length).equals(raw)) throw new Error('RLE round trip failed');
  const head = Buffer.alloc(24);
  head.writeUInt8(0x19, 0);
  head.writeUInt8(0x14, 1);
  head.writeUInt16LE(0x08, 2);
  head.writeUInt16LE(w, 4);
  head.writeUInt16LE(h, 6);
  head.writeUInt16LE(w * 2, 8);
  head.writeUInt32LE(1, 12);
  head.writeUInt32LE(body.length, 16);
  head.writeUInt32LE(raw.length, 20);
  return Buffer.concat([head, body]);
}

// Straight-alpha RGBA -> RGB565 plane then A8 plane.
export function raw565a8({ w, h, rgba }) {
  const n = w * h, raw = Buffer.alloc(n * 3);
  for (let i = 0; i < n; i++) {
    const r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
    raw.writeUInt16LE(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3), i * 2);  // little-endian RGB565, as LVGL stores it
    raw[n * 2 + i] = rgba[i * 4 + 3];
  }
  return raw;
}

// ---- scene scale ---------------------------------------------------------------------------------------------
// Scenes are laid out at the original 1x art size. PET_SCALE then scales them at render time: frames render bigger
// (the parts are vector, so they stay sharp), and positions and the scene box grow by the same factor about the box
// origin, so the box centre stays centred on the glass. One factor for every parts-built scene keeps Clawd one size.
export const PET_SCALE = 1.5;

// An SVG frame at k times its size. Its canvas widens to whole pixels (transparent margin). A frame without a
// viewBox (the 1x1 blank) stays as it is.
export function scaleSvg(svg, k) {
  return svg.replace(/<svg[^>]*>/, (t) => {
    const vb = t.match(/\bviewBox="([^"]*)"/);
    if (!vb) return t;
    const [x, y, w, h] = vb[1].trim().split(/\s+/).map(Number);
    const W = Math.ceil(w * k - 1e-9), H = Math.ceil(h * k - 1e-9);
    return t.replace(/\bwidth="[^"]*"/, `width="${W}"`).replace(/\bheight="[^"]*"/, `height="${H}"`)
      .replace(vb[0], `viewBox="${x} ${y} ${W / k} ${H / k}"`);
  });
}

// Scale one scene in place: the frame sets it owns, its layers' positions and its box. A set shared with another
// scene module is scaled by the module that owns it, so pass only this module's own sets.
export function scaleScene(k, sets, layers, scene) {
  const sz = (v) => Math.round(v * k);
  for (const frames of Object.values(sets)) for (const f of frames) f.svg = scaleSvg(f.svg, k);
  for (const l of layers) l.at = l.at.map(([x, y]) => [sz(x), sz(y)]);
  Object.assign(scene, { w: sz(scene.w), h: sz(scene.h), dx: sz(scene.dx), dy: sz(scene.dy) });
}
