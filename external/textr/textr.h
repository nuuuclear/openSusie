/*
 * textr.h ~ a tiny single header text renderer for SDL3 using stb_truetype
 *
 * - UTF-8 input (latin, kana, kanji, and anything the font would have)
 * - Glyphs are simply rasterized and packed into 1024x1024 atlas pages
 * - Multiple fonts with per codepoint fallback
 * - .ttf / .otf / .ttc (CFF1 and glyf outlines). no variable axis support.
 * - Newlines, kerning (legacy 'kern' table), measuring, color
 *
 * Usage:
 *   In exactly one .c file:
 *       #define TEXTR_IMPLEMENTATION
 *       #include "textr.h"
 *   (needs stb_truetype.h on the include path)
 *
 *   TR_Font *f = TR_CreateFont(renderer, 28.0f);       // size in pixels (em size)
 *   TR_AddFontFile(f, "NotoSansJP-Regular.ttf", 0);    // first font = primary
 *   TR_AddFontFile(f, "fallback.ttf", 0);              // optional fallbacks
 *   TR_SetRenderMode(f, TR_RENDER_PIXEL_PERFECT);      // render mode
 *   TR_DrawText(f, 20, 20, u8"Hello こんにちは 漢字", (SDL_Color){255,255,255,255});
 *   TR_DestroyFont(f);
 *
 * Coordinates are in renderer units; (x, y) is the top-left of the first line.
 * Not thread-safe. Must be called from the thread that owns the renderer.
 * 
 * (C) Public Domain - Okiwari Group
 *                Version 2025/08/23
*/

#ifndef TEXTR_H
#define TEXTR_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TR_Font TR_Font;

typedef enum {
	TR_RENDER_GRAYSCALE,
	TR_RENDER_MONOCHROME,
	TR_RENDER_PIXEL_PERFECT
} TR_RenderMode;

/* px = em height in pixels. For HiDPI, multiply by the display scale */
TR_Font *TR_CreateFont(SDL_Renderer *renderer, float px);
void TR_DestroyFont(TR_Font *f);

/* Add a font face. the first face defines line metrics.
 * index selects a face inside a .ttc collection (0 for plain files) */
bool TR_AddFontFile(TR_Font *f, const char *path, int index);

/* data must remain alive unless owned is true.
 * Owned data must have been allocated with SDL_malloc. */
bool TR_AddFontMemory(TR_Font *f, void *data, int index, bool owned);

/* Change rendering mode. Changing modes invalidates the glyph atlas. */
bool TR_SetRenderMode(TR_Font *f, TR_RenderMode mode);
TR_RenderMode TR_GetRenderMode(const TR_Font *f);

void TR_DrawText(TR_Font *f, float x, float y, const char *utf8, SDL_Color color);
void TR_MeasureText(TR_Font *f, const char *utf8, float *w, float *h);
float TR_LineHeight(TR_Font *f);

#ifdef __cplusplus
}
#endif

#endif /* TEXTR_H */

/* ====================================================================== */

#ifdef TEXTR_IMPLEMENTATION
#ifndef TEXTR_IMPLEMENTATION_DONE
#define TEXTR_IMPLEMENTATION_DONE

#include <math.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(x, u) ((void)(u), SDL_malloc(x))
#define STBTT_free(x, u) ((void)(u), SDL_free(x))
#define STBTT_assert(x) SDL_assert(x)
#define STBTT_strlen(x) SDL_strlen(x)
#define STBTT_memcpy SDL_memcpy
#define STBTT_memset SDL_memset
#define STBTT_ifloor(x) ((int)SDL_floor(x))
#define STBTT_iceil(x) ((int)SDL_ceil(x))
#define STBTT_sqrt(x) SDL_sqrt(x)
#define STBTT_pow(x, y) SDL_pow(x, y)
#define STBTT_fmod(x, y) SDL_fmod(x, y)
#define STBTT_cos(x) SDL_cos(x)
#define STBTT_acos(x) SDL_acos(x)
#define STBTT_fabs(x) SDL_fabs(x)

#include "stb_truetype.h"

#define TR_ATLAS_SIZE 1024

typedef struct {
	uint32_t cp;
	bool used;

	int face;
	int glyph;

	float adv;

	int page;

	int16_t u, v, w, h;
	int16_t xoff, yoff;
} TR_Glyph;

typedef struct {
	SDL_Texture *tex;
	int cx, cy, rowh;
} TR_Page;

typedef struct {
	stbtt_fontinfo info;
	unsigned char *data;
	bool owned;
	float scale;
} TR_Face;

struct TR_Font {
	SDL_Renderer *r;
	float px;

	TR_RenderMode mode;

	TR_Face *faces;
	int nfaces;

	TR_Page *pages;
	int npages;

	TR_Glyph *tab;
	size_t cap, count;

	float ascent;
	float lineh;
};

/* ---- internal helpers ----------------------------------------------- */

static float tr_line_height(const TR_Font *f) {
	if (f->mode == TR_RENDER_GRAYSCALE)
		return f->lineh;

	return SDL_roundf(f->lineh);
}

static void tr_clear_glyph_cache(TR_Font *f) {
	for (int i = 0; i < f->npages; i++)
		SDL_DestroyTexture(f->pages[i].tex);

	SDL_free(f->pages);
	SDL_free(f->tab);

	f->pages = NULL;
	f->npages = 0;

	f->tab = NULL;
	f->cap = 0;
	f->count = 0;
}

/* ---- font creation --------------------------------------------------- */

TR_Font *TR_CreateFont(SDL_Renderer *renderer, float px) {
	if (!renderer || px <= 0.0f)
		return NULL;

	TR_Font *f = (TR_Font *)SDL_calloc(1, sizeof(TR_Font));
	if (!f)
		return NULL;

	f->r = renderer;
	f->px = px;
	f->mode = TR_RENDER_GRAYSCALE;

	return f;
}

void TR_DestroyFont(TR_Font *f) {
	if (!f)
		return;

	tr_clear_glyph_cache(f);

	for (int i = 0; i < f->nfaces; i++) {
		if (f->faces[i].owned)
			SDL_free(f->faces[i].data);
	}

	SDL_free(f->faces);
	SDL_free(f);
}

bool TR_AddFontMemory(TR_Font *f, void *data, int index, bool owned) {
	if (!f || !data) {
		if (owned)
			SDL_free(data);

		return false;
	}

	TR_Face face;
	SDL_zero(face);

	int off = stbtt_GetFontOffsetForIndex((const unsigned char *)data, index);

	if (off < 0 || !stbtt_InitFont(&face.info, (const unsigned char *)data, off)) {
		if (owned)
			SDL_free(data);

		return false;
	}

	face.data = (unsigned char *)data;
	face.owned = owned;
    face.scale = stbtt_ScaleForMappingEmToPixels(&face.info, f->px);

	if (face.scale <= 0.0f) {
		if (owned)
			SDL_free(data);

		return false;
	}

	TR_Face *nf = (TR_Face *)SDL_realloc(
		f->faces,
		sizeof(TR_Face) * (size_t)(f->nfaces + 1)
	);

	if (!nf) {
		if (owned)
			SDL_free(data);

		return false;
	}

	f->faces = nf;
	f->faces[f->nfaces++] = face;

	if (f->nfaces == 1) {
		int asc, desc, gap;

		stbtt_GetFontVMetrics(&face.info, &asc, &desc, &gap);

		f->ascent = asc * face.scale;
		f->lineh = (asc - desc + gap) * face.scale;
	}

	tr_clear_glyph_cache(f);

	return true;
}

bool TR_AddFontFile(TR_Font *f, const char *path, int index) {
	if (!f || !path)
		return false;

	size_t size = 0;
	void *data = SDL_LoadFile(path, &size);

	if (!data)
		return false;

	return TR_AddFontMemory(f, data, index, true);
}

bool TR_SetRenderMode(TR_Font *f, TR_RenderMode mode) {
	if (!f)
		return false;

	if (mode < TR_RENDER_GRAYSCALE || mode > TR_RENDER_PIXEL_PERFECT)
		return false;

	if (f->mode == mode)
		return true;

	f->mode = mode;
	tr_clear_glyph_cache(f);

	return true;
}

TR_RenderMode TR_GetRenderMode(const TR_Font *f) {
	if (!f)
		return TR_RENDER_GRAYSCALE;

	return f->mode;
}

float TR_LineHeight(TR_Font *f) {
	if (!f)
		return 0.0f;

	return tr_line_height(f);
}

/* ---- UTF-8 ----------------------------------------------------------- */

static uint32_t tr_next_cp(const unsigned char **ps) {
	const unsigned char *s = *ps;
	uint32_t c = *s++;
	int extra;

	if (c < 0x80) {
		*ps = s;
		return c;
	}
	else if ((c & 0xE0) == 0xC0) {
		c &= 0x1F;
		extra = 1;
	}
	else if ((c & 0xF0) == 0xE0) {
		c &= 0x0F;
		extra = 2;
	}
	else if ((c & 0xF8) == 0xF0) {
		c &= 0x07;
		extra = 3;
	}
	else {
		*ps = s;
		return 0xFFFD;
	}

	for (int i = 0; i < extra; i++) {
		if ((*s & 0xC0) != 0x80) {
			*ps = s;
			return 0xFFFD;
		}

		c = (c << 6) | (*s++ & 0x3F);
	}

	*ps = s;
	return c;
}

/* ---- glyph cache ----------------------------------------------------- */

static size_t tr_hash(uint32_t cp) {
	return (size_t)(cp * 2654435761u);
}

static TR_Glyph *tr_slot(TR_Glyph *tab, size_t cap, uint32_t cp) {
	size_t i = tr_hash(cp) & (cap - 1);

	while (tab[i].used && tab[i].cp != cp)
		i = (i + 1) & (cap - 1);

	return &tab[i];
}

static bool tr_grow(TR_Font *f) {
	size_t ncap = f->cap ? f->cap * 2 : 512;

	TR_Glyph *nt = (TR_Glyph *)SDL_calloc(ncap, sizeof(TR_Glyph));
	if (!nt)
		return false;

	for (size_t i = 0; i < f->cap; i++) {
		if (f->tab[i].used)
			*tr_slot(nt, ncap, f->tab[i].cp) = f->tab[i];
	}

	SDL_free(f->tab);

	f->tab = nt;
	f->cap = ncap;

	return true;
}

/* ---- atlas ----------------------------------------------------------- */

static bool tr_new_page(TR_Font *f) {
	TR_Page *np = (TR_Page *)SDL_realloc(
		f->pages,
		sizeof(TR_Page) * (size_t)(f->npages + 1)
	);

	if (!np)
		return false;

	f->pages = np;

	SDL_Texture *t = SDL_CreateTexture(
		f->r,
		SDL_PIXELFORMAT_RGBA32,
		SDL_TEXTUREACCESS_STATIC,
		TR_ATLAS_SIZE,
		TR_ATLAS_SIZE
	);

	if (!t)
		return false;

	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);

	SDL_ScaleMode filter = SDL_SCALEMODE_LINEAR;

	if (f->mode != TR_RENDER_GRAYSCALE)
		filter = SDL_SCALEMODE_NEAREST;

	SDL_SetTextureScaleMode(t, filter);

	void *zero = SDL_calloc(
		1,
		(size_t)TR_ATLAS_SIZE * TR_ATLAS_SIZE * 4
	);

	if (!zero) {
		SDL_DestroyTexture(t);
		return false;
	}

	SDL_UpdateTexture(t, NULL, zero, TR_ATLAS_SIZE * 4);
	SDL_free(zero);

	f->pages[f->npages].tex = t;
	f->pages[f->npages].cx = 0;
	f->pages[f->npages].cy = 0;
	f->pages[f->npages].rowh = 0;

	f->npages++;

	return true;
}

/* simple shelf packer with one pixel of padding. */
static bool tr_pack(TR_Font *f, int w, int h, int *page, int *x, int *y) {
	int pw = w + 1;
	int ph = h + 1;

	if (pw > TR_ATLAS_SIZE || ph > TR_ATLAS_SIZE)
		return false;

	for (int attempt = 0; attempt < 2; attempt++) {
		if (f->npages > 0) {
			TR_Page *p = &f->pages[f->npages - 1];

			if (p->cx + pw > TR_ATLAS_SIZE) {
				p->cx = 0;
				p->cy += p->rowh;
				p->rowh = 0;
			}

			if (p->cy + ph <= TR_ATLAS_SIZE) {
				*page = f->npages - 1;
				*x = p->cx;
				*y = p->cy;

				p->cx += pw;

				if (ph > p->rowh)
					p->rowh = ph;

				return true;
			}
		}

		if (!tr_new_page(f))
			return false;
	}

	return false;
}

/* ---- glyph generation ------------------------------------------------ */

static const TR_Glyph *tr_get(TR_Font *f, uint32_t cp)
{
	if ((f->count + 1) * 2 > f->cap) {
		if (!tr_grow(f))
			return NULL;
	}

	TR_Glyph *g = tr_slot(f->tab, f->cap, cp);

	if (g->used)
		return g;

	int face = 0;
	int gi = 0;

	for (int i = 0; i < f->nfaces; i++) {
		gi = stbtt_FindGlyphIndex(&f->faces[i].info, (int)cp);

		if (gi) {
			face = i;
			break;
		}
	}

	if (!gi)
		face = 0;

	TR_Face *fc = &f->faces[face];

	int adv, lsb;
	stbtt_GetGlyphHMetrics(&fc->info, gi, &adv, &lsb);

	g->used = true;
	g->cp = cp;
	g->face = face;
	g->glyph = gi;
	g->adv = adv * fc->scale;
	g->page = -1;

	f->count++;

	int w, h, xo, yo;

	unsigned char *bm = stbtt_GetGlyphBitmap(
		&fc->info,
		fc->scale,
		fc->scale,
		gi,
		&w,
		&h,
		&xo,
		&yo
	);

	if (!bm)
		return g;

	if (w <= 0 || h <= 0) {
		stbtt_FreeBitmap(bm, NULL);
		return g;
	}

	int page, px, py;

	if (!tr_pack(f, w, h, &page, &px, &py)) {
		stbtt_FreeBitmap(bm, NULL);
		return g;
	}

	uint8_t *rgba = (uint8_t *)SDL_malloc((size_t)w * h * 4);

	if (!rgba) {
		stbtt_FreeBitmap(bm, NULL);
		return g;
	}

	bool binary = f->mode != TR_RENDER_GRAYSCALE;

	for (int i = 0; i < w * h; i++) {
		uint8_t alpha = bm[i];

		if (binary)
			alpha = alpha >= 128 ? 255 : 0;

		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = alpha;
	}

	SDL_Rect rc = { px, py, w, h };

	bool updated = SDL_UpdateTexture(
		f->pages[page].tex,
		&rc,
		rgba,
		w * 4
	);

	SDL_free(rgba);
	stbtt_FreeBitmap(bm, NULL);

	if (!updated)
		return g;

	g->page = page;

	g->u = (int16_t)px;
	g->v = (int16_t)py;
	g->w = (int16_t)w;
	g->h = (int16_t)h;

	g->xoff = (int16_t)xo;
	g->yoff = (int16_t)yo;

	return g;
}

/* ---- layout / drawing ----------------------------------------------- */

static void tr_walk(
	TR_Font *f,
	const char *text,
	float x0,
	float y0,
	SDL_Color c,
	bool draw,
	float *out_w,
	float *out_h
) {
	float x = x0;
	float y = y0;
	float maxw = 0.0f;

	int prev_face = -1;
	int prev_glyph = 0;

	const unsigned char *s = (const unsigned char *)text;

	if (f->nfaces == 0 || !text) {
		if (out_w)
			*out_w = 0.0f;

		if (out_h)
			*out_h = 0.0f;

		return;
	}

	bool pixel_perfect = f->mode == TR_RENDER_PIXEL_PERFECT;
	bool integer_position = f->mode != TR_RENDER_GRAYSCALE;

	if (pixel_perfect) {
		x = SDL_roundf(x);
		y = SDL_roundf(y);
	}

	while (*s) {
		uint32_t cp = tr_next_cp(&s);

		if (cp == '\r')
			continue;

		if (cp == '\n') {
			if (x - x0 > maxw)
				maxw = x - x0;

			x = x0;
			y += tr_line_height(f);

			if (pixel_perfect) {
				x = SDL_roundf(x);
				y = SDL_roundf(y);
			}

			prev_face = -1;
			prev_glyph = 0;

			continue;
		}

		const TR_Glyph *g = tr_get(f, cp);

		if (!g)
			continue;

		if (prev_face == g->face && prev_glyph) {
			TR_Face *fc = &f->faces[g->face];

			float kern = stbtt_GetGlyphKernAdvance(
				&fc->info,
				prev_glyph,
				g->glyph
			) * fc->scale;

			x += kern;

			if (pixel_perfect)
				x = SDL_roundf(x);
		}

		if (draw && g->page >= 0) {
			SDL_Texture *t = f->pages[g->page].tex;

			SDL_SetTextureColorMod(t, c.r, c.g, c.b);
			SDL_SetTextureAlphaMod(t, c.a);

			SDL_FRect src = {
				(float)g->u,
				(float)g->v,
				(float)g->w,
				(float)g->h
			};

			float dst_x = x + g->xoff;
			float dst_y = y + f->ascent + g->yoff;

			if (integer_position) {
				dst_x = SDL_roundf(dst_x);
				dst_y = SDL_roundf(dst_y);
			}

			SDL_FRect dst = {
				dst_x,
				dst_y,
				(float)g->w,
				(float)g->h
			};

			SDL_RenderTexture(f->r, t, &src, &dst);
		}

		x += g->adv;

		if (pixel_perfect)
			x = SDL_roundf(x);

		prev_face = g->face;
		prev_glyph = g->glyph;
	}

	if (x - x0 > maxw)
		maxw = x - x0;

	if (out_w)
		*out_w = maxw;

	if (out_h)
		*out_h = (y - y0) + tr_line_height(f);
}

void TR_DrawText(
	TR_Font *f,
	float x,
	float y,
	const char *utf8,
	SDL_Color color
) {
	if (!f || !utf8)
		return;

	tr_walk(f, utf8, x, y, color, true, NULL, NULL);
}

void TR_MeasureText(
	TR_Font *f,
	const char *utf8,
	float *w,
	float *h
) {
	if (!f) {
		if (w)
			*w = 0.0f;

		if (h)
			*h = 0.0f;

		return;
	}

	SDL_Color c = { 0, 0, 0, 0 };

	tr_walk(f, utf8, 0.0f, 0.0f, c, false, w, h);
}

#endif /* TEXTR_IMPLEMENTATION_DONE */
#endif /* TEXTR_IMPLEMENTATION */