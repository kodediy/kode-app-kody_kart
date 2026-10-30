// Sprite pipeline for Kody Kart on the Dot.
// Run from the repo root: node firmware/apps/kody-kart/tools/gen-sprites.mjs
//
// Emits main/sprites_gen.h (palette-indexed sprites) and tools/preview.png.
//
// The kart with Kody at the wheel is not drawn by hand: it is a small voxel
// model (a CSG of boxes, cylinders and spheres, below) ray-cast from KART_VIEWS
// directions around it, a little from above, with one light and a dark
// outline. So every view agrees with every other, and a rival is the same
// sprite with another palette (the indices BODY_* and FUR_* are swapped at
// draw time). Kody is the rig's Kody (games/shared/kody-rig.js) in the round:
// blue, round ears, white eyes with ink pupils, two white teeth.
import sharp from "sharp";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));

// ---------------------------------------------------------------- palette
// Indices the C side knows (render.c's character palettes fill 5..10).
const NONE = 0, INK = 1, WHITE = 2, GRAY = 3, DARK = 4;
const BODY_L = 5, BODY = 6, BODY_D = 7, FUR_L = 8, FUR = 9, FUR_D = 10, SHADOW = 11;
// For the preview only: Kody's own colours.
const PREVIEW = {
  [INK]: [0x19, 0x19, 0x19], [WHITE]: [0xff, 0xff, 0xff], [GRAY]: [0x9a, 0x9a, 0xa4], [DARK]: [0x55, 0x55, 0x60],
  [BODY_L]: [0xff, 0xd0, 0x60], [BODY]: [0xff, 0xac, 0x00], [BODY_D]: [0xc0, 0x7a, 0x00],
  [FUR_L]: [0x3d, 0xb0, 0xe0], [FUR]: [0x02, 0x8a, 0xc2], [FUR_D]: [0x01, 0x62, 0x8a], [SHADOW]: [0x40, 0x40, 0x40],
};

// ---------------------------------------------------------------- the model
// Kart space: +x is forward, +y is the driver's right, +z is up; the ground
// is z = 0. One unit is about one pixel of the unscaled sprite.
const sq = (v) => v * v;
const sphere = (x, y, z, cx, cy, cz, r) => sq(x - cx) + sq(y - cy) + sq(z - cz) <= r * r;
const HEAD = { x: -1.5, z: 13.6, r: 4.7 };

/** @returns a palette "material" at (x, y, z): 0 empty, else INK/WHITE/GRAY or "body"/"fur". */
function material(x, y, z) {
  const ay = Math.abs(y);
  // Wheels: tyres round an axle along y, a grey hub on the outside.
  for (const wx of [-7.5, 7.5]) {
    const r2 = sq(x - wx) + sq(z - 2.8);
    if (ay >= 5.6 && ay <= 9.2 && r2 <= sq(2.9)) return r2 <= sq(1.2) && ay > 8.4 ? GRAY : INK;
  }
  // Kody: his round body in the seat, his big head, round ears, his paws on the wheel.
  if (sphere(x, y, z, HEAD.x, 0, HEAD.z, HEAD.r)) {
    const front = x - HEAD.x;
    // Eyes: white with an ink pupil, on the front of the head.
    for (const ey of [-1.8, 1.8]) {
      if (front > 3.0 && Math.abs(y - ey) < 1.15 && z > 13.4 && z < 16.0)
        return Math.abs(y - ey) < 0.55 && z > 13.8 && z < 15.1 ? INK : WHITE;
    }
    // Teeth.
    if (front > 3.4 && ay < 0.9 && z > 11.0 && z < 12.2 && ay > 0.12) return WHITE;
    return "fur";
  }
  if (sphere(x, y, z, -2.5, -3.7, 17.6, 1.6) || sphere(x, y, z, -2.5, 3.7, 17.6, 1.6)) return "fur";
  if (sq((x + 3.2) / 4.2) + sq(y / 4.6) + sq((z - 8.2) / 4.0) <= 1) return "fur";
  if (sphere(x, y, z, 1.6, -2.4, 8.6, 1.2) || sphere(x, y, z, 1.6, 2.4, 8.6, 1.2)) return "fur";
  // The steering wheel.
  if (x > 2.4 && x < 3.4 && ay < 2.6 && z > 7.2 && z < 9.6 && !(ay < 1.2 && z > 7.8 && z < 9.0)) return INK;
  // The seat back behind him.
  if (x > -9.5 && x < -6.5 && ay < 4.2 && z >= 4.5 && z < 10) return "body";
  // Exhausts.
  if (x > -12.5 && x < -9.0 && ay > 1.8 && ay < 4.0 && z > 4.0 && z < 6.2) return GRAY;
  // The chassis: a tub that narrows to the nose, a grey bumper.
  if (x > -10.5 && x < 10 && z >= 2.2 && z < 5.0 && ay < 6.4 - Math.max(0, x - 5.5) * 0.7) return "body";
  if (x >= 10 && x < 12 && ay < 3.6 && z >= 2.2 && z < 4.2) return GRAY;
  // Side pods between the wheels.
  if (x > -4.5 && x < 4.5 && ay >= 6.4 && ay < 8.2 && z >= 2.2 && z < 4.4) return "body";
  return NONE;
}
const solid = (x, y, z) => material(x, y, z) !== NONE;

// ---------------------------------------------------------------- ray casting
export const KART_VIEWS = 16;
const SW = 32, SH = 32, OX = 16, OY = 25;      // sprite size; where the kart's ground origin lands
const ELEV = (24 * Math.PI) / 180;            // looked at a little from above

const norm = (v) => { const l = Math.hypot(...v); return v.map((c) => c / l); };
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const add = (a, b, k = 1) => a.map((c, i) => c + b[i] * k);

/**
 * View i looks at the kart from azimuth i * 360 / KART_VIEWS: view 0 from
 * straight behind (the eye looks along +x), increasing clockwise seen from
 * above in this y-right world: view 4 sees its right side.
 */
function render(view) {
  const phi = (view * 2 * Math.PI) / KART_VIEWS;
  // Behind the kart means looking along +x; going round clockwise, the eye looks along (cos, -sin)...
  // ...so view 4 (90 deg) looks along -y: it sees the kart's right (+y) side.
  const f = norm([Math.cos(phi) * Math.cos(ELEV), -Math.sin(phi) * Math.cos(ELEV), -Math.sin(ELEV)]);
  const r = norm(cross(f, [0, 0, 1]).map((c) => -c));   // screen right
  const u = cross(f, r);                                // screen up
  const light = norm([0, 1, 2].map((i) => -0.5 * r[i] + u[i] - 0.7 * f[i]));   // from up-left, toward the eye
  const px = new Uint8Array(SW * SH);
  for (let sy = 0; sy < SH; sy++)
    for (let sx = 0; sx < SW; sx++) {
      // The pixel's ray: through the plane at the origin, from far behind it.
      const o = add(add([0, 0, 0], r, sx + 0.5 - OX), u, OY - (sy + 0.5));
      let p = add(o, f, -40);
      for (let t = 0; t < 80; t += 0.2, p = add(p, f, 0.2)) {
        if (p[2] < 0) {
          // The ground: a soft shadow under the kart.
          if (sq(p[0] / 12) + sq(p[1] / 9.5) <= 1) px[sy * SW + sx] = SHADOW;
          break;
        }
        const m = material(p[0], p[1], p[2]);
        if (m === NONE) continue;
        if (m === "body" || m === "fur") {
          const e = 1.0;
          const n = norm([
            (solid(p[0] - e, p[1], p[2]) ? 1 : 0) - (solid(p[0] + e, p[1], p[2]) ? 1 : 0),
            (solid(p[0], p[1] - e, p[2]) ? 1 : 0) - (solid(p[0], p[1] + e, p[2]) ? 1 : 0),
            (solid(p[0], p[1], p[2] - e) ? 1 : 0) - (solid(p[0], p[1], p[2] + e) ? 1 : 0) + 0.001,
          ]);
          const d = n[0] * light[0] + n[1] * light[1] + n[2] * light[2];
          const tone = d > 0.55 ? 0 : d > -0.1 ? 1 : 2;
          px[sy * SW + sx] = (m === "body" ? BODY_L : FUR_L) + tone;
        } else {
          px[sy * SW + sx] = m;
        }
        break;
      }
    }
  // An ink outline round the solid pixels, so a kart reads on any ground.
  const out = px.slice();
  for (let y = 0; y < SH; y++)
    for (let x = 0; x < SW; x++) {
      const c = px[y * SW + x];
      if (c !== NONE && c !== SHADOW) continue;
      const near = [[1, 0], [-1, 0], [0, 1], [0, -1]].some(([dx, dy]) => {
        const nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= SW || ny >= SH) return false;
        const n = px[ny * SW + nx];
        return n !== NONE && n !== SHADOW;
      });
      if (near) out[y * SW + x] = INK;
    }
  return out;
}

const views = Array.from({ length: KART_VIEWS }, (_, i) => render(i));

// ---------------------------------------------------------------- items
// Drawn by hand, one character per pixel ('.' is none). Their own palette.
const ITEM_PAL = [
  ["none", null], ["ink", "#191919"], ["white", "#FFFFFF"], ["yellow", "#FFAC00"], ["gold", "#FFD84A"],
  ["orange", "#E07800"], ["brown", "#7A4A24"], ["tan", "#D8A060"], ["red", "#C9303D"], ["blue", "#028AC2"],
  ["shade", "#B87000"], ["green", "#4CA238"], ["leaf", "#2C7A32"], ["light", "#7CC860"], ["gray", "#55555F"],
];
const ITEM_KEY = { ".": 0, I: 1, W: 2, Y: 3, G: 4, O: 5, B: 6, T: 7, R: 8, K: 9, S: 10, N: 11, D: 12, L: 13, X: 14 };
const ITEMS = {
  // The item box: a yellow cube with a white "?" (it bobs in the game).
  box: `
..IIIIIIIIIIII..
.IGGGGGGGGGGGGI.
IGYYYYYYYYYYYYSI
IGYYYYIIIIYYYYSI
IGYYYIWWWWIYYYSI
IGYYIWWIIWWIYYSI
IGYYYIIYIWWIYYSI
IGYYYYYYIWWIYYSI
IGYYYYYIWWIYYYSI
IGYYYYYIWWIYYYSI
IGYYYYYYIIYYYYSI
IGYYYYYIWWIYYYSI
IGYYYYYIWWIYYYSI
IGYYYYYYIIYYYYSI
.ISSSSSSSSSSSSI.
..IIIIIIIIIIII..`,
  // A sunflower seed, Kodegotchi's: striped ink and white.
  seed: `
...I...
..IWI..
.IWIWI.
.IWIWI.
IWIWIWI
IWIWIWI
IWIWIWI
.IWIWI.
..III..`,
  golden: `
...I...
..IGI..
.IGWGI.
.IGYGI.
IGYGYGI
IGYGYGI
IGYGYGI
.IGYGI.
..III..`,
  // A banana peel lying on the road.
  peel: `
.....II.......
....IBYI......
....IYYI......
...IYYYYI.....
..IYYWYYYI....
.IYYIYYIYYI...
IYYIIYYIIYYI..
IYI.IYYI.IYYI.
.I..IYSI..IYI.
....IISI...I..
.....II.......`,
  // An acorn: a hatched cap, a tan nut.
  acorn: `
....II....
...IBBI...
.IIBBBBII.
IBSBSBSBSI
IBBSBSBSBI
.IIIIIIII.
.ITTTTTTI.
.ITWTTTTI.
.ITTTTTTI.
..ITTTTI..
...ITTI...
....II....`,
  // By the track: a round tree, a bush, a Kode sign, a stack of tyres.
  tree: `
.....IIIIII.....
...IILLLLNNII...
..ILLLNNNNNNDI..
.ILLNNNNNNNNNDI.
.ILNNNNNNNNNDDI.
ILLNNNNNNNNNNDDI
ILNNNNNNNNNNDNDI
ILNNNNNNNNNDDDDI
ILNNNNNNNNNNDDDI
INNNNNNNNNNDDDDI
.INNNNNNNNDDDDI.
.IDNNNNDNDDDDDI.
..IDDDDDDDDDDI..
...IIDDDDDDII...
.....IIBBII.....
......IBBI......
......IBSI......
......IBSI......
......IBSI......
.....IIBSII.....`,
  bush: `
...IIII.IIII..
..ILLNNINNNDI.
.ILNNNNNNNNNDI
ILNNNNNNNNNNDI
INNNNNNNNNNDDI
IDNNNNNNNDDDDI
.IIIIIIIIIIII.`,
  sign: `
IIIIIIIIIIIIIIIIIIII
IYYYYYYYYYYYYYYYYYSI
IYYYYYIKKIYYIKKIYYSI
IYYYYYIKKIYIKKIYYYSI
IYYYYYIKKIIKKIYYYYSI
IYYYYYIKKKKKIYYYYYSI
IYYYYYIKKKKKIYYYYYSI
IYYYYYIKKIIKKIYYYYSI
IYYYYYIKKIYIKKIYYYSI
IYYYYYIKKIYYIKKIYYSI
ISSSSSSSSSSSSSSSSSSI
IIIIIIIIIIIIIIIIIIII
...IXI........IXI...
...IXI........IXI...
...IXI........IXI...`,
  tyres: `
.IIIIIIII.
IXXXXXXXXI
IRRRRRRRRI
IXXXXXXXXI
IWWWWWWWWI
IXXXXXXXXI
IRRRRRRRRI
IXXXXXXXXI
.IIIIIIII.`,
  // The beach's: a palm, a striped umbrella.
  palm: `
...II.....II.....
..ILLI...ILNI....
.ILNNNI.INNNDI...
ILNI.INIDNIIDI...
.II...INNDI..II..
.....IINNDDI.....
....ILNIIIDNI....
...ILNI.B.INDI...
..ILI..IBI..IDI..
...I...IBSI..I...
.......IBSI......
.......IBSI......
.......IBBSI.....
........IBSI.....
........IBSI.....
........IBSI.....
........IBBSI....
.........IBSI....
.........IBSI....
........IIBSII...`,
  umbrella: `
......IIII......
....IIRWRWII....
..IIRRWWRRWWII..
.IRRRWWWRRRWWWI.
IRRRRWWWRRRRWWWI
IIIIIIIIIIIIIIII
.......IXI......
.......IXI......
.......IXI......
.......IXI......
.......IXI......
......IIXII.....`,
  // The snow's: a snowy pine, a snowman.
  pine: `
.......II.......
......IWWI......
......IDWI......
.....IWWDDI.....
....IDDWWDDI....
.....IWDDDI.....
....IWWWDDDI....
...IDDWWWDDDI...
....IWDDDDDI....
...IWWWDDDDDI...
..IDDWWWWDDDDI..
...IWWDDDDDDI...
..IWWWWDDDDDDI..
.IDDWWWWWDDDDDI.
IIIIIIIIIIIIIIII
.......IBI......
.......IBI......
......IIBII.....`,
  snowman: `
....IIIII....
...IXXXXXI...
...IXXXXXI...
..IIIIIIIII..
...IWWWWWI...
..IWIWWIWWI..
..IWWWOOWWI..
...IWWWWWI...
..IRRRRRRRI..
.IWWWWIWWWWI.
.IWWWWWWWWWI.
.IWWWWIWWWWI.
IWWWWWWWWWWWI
IWWWWWIWWWWWI
IWWWWWWWWWWWI
.IWWWWWWWWWI.
..IIIIIIIII..`,
  // The turbo: a sunflower.
  turbo: `
.....IYI.....
..IYIIYIIYI..
..IYYIYIYYI..
.IIIYYYYYIII.
IYYYIIIIIYYYI
.IIYIBBBIYII.
IYYYIBTBIYYYI
.IIYIBBBIYII.
IYYYIIIIIYYYI
.IIIYYYYYIII.
..IYYINIYYI..
..IYIINIIYI..
......N......`,
};
function itemSprite(text) {
  const rows = text.trim().split("\n");
  const w = rows[0].length, h = rows.length, px = new Uint8Array(w * h);
  rows.forEach((row, y) => {
    if (row.length !== w) throw new Error(`ragged sprite row ${y}`);
    [...row].forEach((ch, x) => {
      if (!(ch in ITEM_KEY)) throw new Error(`unknown pixel '${ch}'`);
      px[y * w + x] = ITEM_KEY[ch];
    });
  });
  return { w, h, px };
}
const items = Object.fromEntries(Object.entries(ITEMS).map(([k, v]) => [k, itemSprite(v)]));

// ---------------------------------------------------------------- header
let h = `/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file sprites_gen.h
 * @brief The kart and item sprites, palette-indexed.
 *
 * Generated by tools/gen-sprites.mjs - do not edit.
 */
#ifndef SPRITES_GEN_H_
#define SPRITES_GEN_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Palette indices: 0 none, then the fixed colours, then a character's six. */
enum { SPR_NONE = 0, SPR_INK, SPR_WHITE, SPR_GRAY, SPR_DARK,
       SPR_BODY_L, SPR_BODY, SPR_BODY_D, SPR_FUR_L, SPR_FUR, SPR_FUR_D, SPR_SHADOW, SPR_COLOURS };

#define KART_VIEWS ${KART_VIEWS}
#define KART_SW ${SW}
#define KART_SH ${SH}
#define KART_OX ${OX}   /* the kart's ground origin in the sprite */
#define KART_OY ${OY}

/* View i: the kart seen from i * 360 / KART_VIEWS degrees, 0 = from behind, clockwise from above. */
static const uint8_t KART_PX[KART_VIEWS][KART_SW * KART_SH] = {
`;
for (const v of views) {
  h += "    {";
  for (let i = 0; i < v.length; i++) h += (i % 32 === 0 ? "\n        " : "") + v[i] + ",";
  h += "\n    },\n";
}
h += "};\n";

h += `
/* Item sprites: their own palette (ITEM_PAL, RGB888), index 0 is none. */
#define ITEM_COLOURS ${ITEM_PAL.length}
static const uint32_t ITEM_PAL[ITEM_COLOURS] = { ${ITEM_PAL.map(([, c]) => (c ? "0x" + c.slice(1) : "0")).join(", ")} };
typedef struct { uint8_t w, h; const uint8_t *px; } item_sprite_t;
`;
for (const [name, sp] of Object.entries(items)) {
  h += `static const uint8_t SPR_${name.toUpperCase()}_PX[${sp.w * sp.h}] = { ${[...sp.px].join(",")} };\n`;
  h += `static const item_sprite_t SPR_${name.toUpperCase()} = { ${sp.w}, ${sp.h}, SPR_${name.toUpperCase()}_PX };\n`;
}
h += `
#ifdef __cplusplus
}
#endif

#endif /* SPRITES_GEN_H_ */
`;
fs.writeFileSync(path.join(HERE, "../main/sprites_gen.h"), h);

// ---------------------------------------------------------------- preview
const SCALE = 6, COLS = 8;
const W = COLS * SW * SCALE, H = Math.ceil(KART_VIEWS / COLS) * SH * SCALE;
const buf = Buffer.alloc(W * H * 3);
for (let i = 0; i < W * H; i++) buf.set([0x8a, 0x8a, 0x92], i * 3);
views.forEach((v, n) => {
  const bx = (n % COLS) * SW * SCALE, by = Math.floor(n / COLS) * SH * SCALE;
  for (let y = 0; y < SH * SCALE; y++)
    for (let x = 0; x < SW * SCALE; x++) {
      const c = v[Math.floor(y / SCALE) * SW + Math.floor(x / SCALE)];
      if (c) buf.set(PREVIEW[c], ((by + y) * W + bx + x) * 3);
    }
});
// The items, under the karts.
const IPAL = ITEM_PAL.map(([, c]) => (c ? [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16)) : null));
const ROW = Math.ceil(KART_VIEWS / COLS) * SH * SCALE;
// Lay the items out in lines that wrap at the sheet's width.
const LINE = 24 * SCALE;
const spots = [];
let ix = 4 * SCALE, iy = ROW + SCALE;
for (const sp of Object.values(items)) {
  if (ix + sp.w * SCALE > W) { ix = 4 * SCALE; iy += LINE; }
  spots.push([sp, ix, iy]);
  ix += (sp.w + 6) * SCALE;
}
const H2 = iy + LINE;
const buf2 = Buffer.alloc(W * H2 * 3);
buf.copy(buf2, 0, 0, buf.length);
for (let i = W * ROW; i < W * H2; i++) buf2.set([0x8a, 0x8a, 0x92], i * 3);
for (const [sp, x0, y0] of spots)
  for (let y = 0; y < sp.h * SCALE; y++)
    for (let x = 0; x < sp.w * SCALE; x++) {
      const c = sp.px[Math.floor(y / SCALE) * sp.w + Math.floor(x / SCALE)];
      if (c) buf2.set(IPAL[c], ((y0 + y) * W + x0 + x) * 3);
    }
await sharp(buf2, { raw: { width: W, height: H2, channels: 3 } }).png().toFile(path.join(HERE, "preview.png"));
console.log(`${KART_VIEWS} kart views + ${Object.keys(items).length} items -> main/sprites_gen.h, tools/preview.png`);
