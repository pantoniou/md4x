/*
 * MD4X: Markdown parser for C
 * (http://github.com/unjs/md4x)
 *
 * Copyright (c) 2026 Pooya Parsa <pooya@pi0.io>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>

#include "md4x-stream.h"
#include "md4x-ansi.h"

/******************************
 ***  Growable byte buffer  ***
 ******************************/

typedef struct {
    char* data;
    size_t size; /* used */
    size_t cap;  /* allocated */
    int error;   /* sticky allocation failure flag */
} STREAM_BUF;

static void
sbuf_init(STREAM_BUF* b)
{
    b->data = NULL;
    b->size = 0;
    b->cap = 0;
    b->error = 0;
}

static void
sbuf_fini(STREAM_BUF* b)
{
    free(b->data);
    b->data = NULL;
    b->size = 0;
    b->cap = 0;
}

static void
sbuf_reset(STREAM_BUF* b)
{
    b->size = 0;
    b->error = 0;
}

static int
sbuf_append(STREAM_BUF* b, const char* data, size_t size)
{
    if(size == 0)
        return 0;
    if(b->size + size > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256;
        char* p;
        while(nc < b->size + size) nc *= 2;
        p = (char*) realloc(b->data, nc);
        if(p == NULL) { b->error = 1; return -1; }
        b->data = p;
        b->cap = nc;
    }
    memcpy(b->data + b->size, data, size);
    b->size += size;
    return 0;
}

/* process_output callback for the ANSI renderer: append into a STREAM_BUF. */
static void
sbuf_sink(const char* text, unsigned size, void* userdata)
{
    sbuf_append((STREAM_BUF*) userdata, text, (size_t) size);
}

/********************
 ***  Context     ***
 ********************/

struct MD4X_STREAM {
    unsigned parser_flags;
    unsigned renderer_flags;
    int width;            /* resolved (AUTO replaced by a concrete value at create) */
    int heal;

    STREAM_BUF accum;     /* accumulated input */
    STREAM_BUF tail;      /* render of accum[anchor:] (active region) */
    STREAM_BUF seg;       /* render of a candidate committed segment */
    STREAM_BUF out;       /* slice returned to the caller (committed/preview/finish) */
    size_t anchor;        /* input offset of the last confirmed sync point */
    int emitted;          /* non-zero once any committed output has been returned */
    int finished;
};

/* Render `input_size` bytes of input into `buf`. When `heal` is set, the
 * renderer's heal-before-render path closes dangling markers first. 0 / -1. */
static int
stream_render(MD4X_STREAM* s, STREAM_BUF* buf, const char* input,
              size_t input_size, int heal)
{
    unsigned flags = s->renderer_flags;
    int ret;

    if(heal)
        flags |= MD_ANSI_FLAG_HEAL;
    else
        flags &= ~(unsigned) MD_ANSI_FLAG_HEAL;

    sbuf_reset(buf);
    ret = md_ansi_ex(input, (unsigned) input_size,
                     sbuf_sink, buf, s->parser_flags, flags, s->width);
    if(ret != 0 || buf->error)
        return -1;
    return 0;
}

/* Does the line [s, s+n) begin a list item (after optional indentation)? */
static int
line_is_list_item(const char* s, size_t n)
{
    size_t i = 0, j;
    while(i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    if(i < n && (s[i] == '-' || s[i] == '*' || s[i] == '+')) {
        return (i + 1 >= n) || s[i + 1] == ' ' || s[i + 1] == '\t';
    }
    j = i;
    while(j < n && s[j] >= '0' && s[j] <= '9') j++;
    return (j > i && j < n && (s[j] == '.' || s[j] == ')'));
}

/* Furthest "safe sync point" offset greater than `from`: the offset just after
 * a blank line that (a) is not inside a fenced code block, and (b) is followed
 * by a complete next line that starts at column 0, is non-blank and is not a
 * list item. At such a point all block containers are closed, so the renderer
 * is in its initial state and the suffix renders standalone identically to the
 * tail of a full render. Returns `from` if there is no such point. */
static size_t
next_sync_offset(const char* data, size_t size, size_t from)
{
    size_t i = 0, line_start = 0, best = from;
    int in_fence = 0;
    char fence_ch = 0;

    while(i <= size) {
        if(i == size || data[i] == '\n') {
            const char* line = data + line_start;
            size_t llen = (size_t)(i - line_start);
            size_t k = 0;
            int is_fence;
            while(k < llen && (line[k] == ' ' || line[k] == '\t')) k++;
            is_fence = (llen - k >= 3)
                && ((line[k] == '`' && line[k + 1] == '`' && line[k + 2] == '`')
                    || (line[k] == '~' && line[k + 1] == '~' && line[k + 2] == '~'));

            if(in_fence) {
                if(is_fence && line[k] == fence_ch) in_fence = 0;
            } else if(is_fence) {
                in_fence = 1;
                fence_ch = line[k];
            } else if(i < size) {
                /* Blank line (only spaces/tabs/CR)? */
                int blank = 1;
                size_t b = line_start;
                while(b < i) {
                    char c = data[b];
                    if(c != ' ' && c != '\t' && c != '\r') { blank = 0; break; }
                    b++;
                }
                if(blank) {
                    size_t nxt = i + 1;
                    if(nxt < size && data[nxt] != ' ' && data[nxt] != '\t' && data[nxt] != '\n') {
                        const char* nl = (const char*) memchr(data + nxt, '\n', size - nxt);
                        if(nl != NULL && !line_is_list_item(data + nxt, (size_t)(nl - (data + nxt))))
                            best = nxt;   /* keep the furthest qualifying point */
                    }
                }
            }
            line_start = i + 1;
        }
        i++;
    }
    return best;
}

MD4X_STREAM*
md4x_stream_create(const MD4X_STREAM_OPTS* opts)
{
    MD4X_STREAM* s = (MD4X_STREAM*) calloc(1, sizeof(MD4X_STREAM));
    int width;

    if(s == NULL)
        return NULL;

    if(opts != NULL) {
        s->parser_flags = opts->parser_flags;
        s->renderer_flags = opts->renderer_flags;
        width = opts->width;
        s->heal = opts->heal;
    } else {
        s->parser_flags = MD_DIALECT_ALL;
        s->renderer_flags = 0;
        width = MD_ANSI_WIDTH_AUTO;
        s->heal = 1;
    }
    if(opts != NULL && opts->parser_flags == 0)
        s->parser_flags = MD_DIALECT_ALL;

    /* Resolve AUTO to a concrete width once, so a mid-stream terminal resize
     * cannot change the layout (and thus the bytes) of already-committed output. */
    if(width == MD_ANSI_WIDTH_AUTO)
        width = md_ansi_detect_width();
    s->width = width;

    sbuf_init(&s->accum);
    sbuf_init(&s->tail);
    sbuf_init(&s->seg);
    sbuf_init(&s->out);
    s->anchor = 0;
    s->emitted = 0;
    s->finished = 0;
    return s;
}

void
md4x_stream_destroy(MD4X_STREAM* s)
{
    if(s == NULL)
        return;
    sbuf_fini(&s->accum);
    sbuf_fini(&s->tail);
    sbuf_fini(&s->seg);
    sbuf_fini(&s->out);
    free(s);
}

/* Build the returned slice in s->out: an inter-block "\n" separator (when some
 * committed output already preceded it) followed by `data`. */
static int
stream_emit(MD4X_STREAM* s, const char* data, size_t size)
{
    sbuf_reset(&s->out);
    if(s->emitted && sbuf_append(&s->out, "\n", 1) != 0)
        return -1;
    if(sbuf_append(&s->out, data, size) != 0)
        return -1;
    return 0;
}

int
md4x_stream_push(MD4X_STREAM* s, const char* chunk, size_t len,
                 const char** out, size_t* out_len)
{
    size_t sync;

    if(out) *out = NULL;
    if(out_len) *out_len = 0;
    if(s == NULL || s->finished)
        return -1;

    if(len > 0 && sbuf_append(&s->accum, chunk, len) != 0)
        return -1;

    sbuf_reset(&s->out);

    /* Render the active region (from the last sync point) for verification. */
    if(stream_render(s, &s->tail, s->accum.data + s->anchor,
                     s->accum.size - s->anchor, 0) != 0)
        return -1;

    /* Try to advance the anchor to the furthest safe sync point. Commit the
     * segment between the old and new anchor, but only after verifying its
     * standalone render is a true prefix of the active-region render (so a
     * construct that unexpectedly spans the point is never committed). */
    sync = next_sync_offset(s->accum.data, s->accum.size, s->anchor);
    if(sync > s->anchor) {
        if(stream_render(s, &s->seg, s->accum.data + s->anchor,
                         sync - s->anchor, 0) != 0)
            return -1;
        if(s->seg.size <= s->tail.size
           && memcmp(s->seg.data, s->tail.data, s->seg.size) == 0) {
            if(stream_emit(s, s->seg.data, s->seg.size) != 0)
                return -1;
            s->anchor = sync;
            s->emitted = 1;
        }
    }

    if(out) *out = s->out.data;
    if(out_len) *out_len = s->out.size;
    return 0;
}

int
md4x_stream_preview(MD4X_STREAM* s, const char** out, size_t* out_len)
{
    if(out) *out = NULL;
    if(out_len) *out_len = 0;
    if(s == NULL)
        return -1;

    /* Healed render of the active region (everything after the last commit),
     * with the inter-block separator so it sits below committed output. */
    if(stream_render(s, &s->tail, s->accum.data + s->anchor,
                     s->accum.size - s->anchor, s->heal) != 0)
        return -1;
    if(stream_emit(s, s->tail.data, s->tail.size) != 0)
        return -1;

    if(out) *out = s->out.data;
    if(out_len) *out_len = s->out.size;
    return 0;
}

int
md4x_stream_finish(MD4X_STREAM* s, const char** out, size_t* out_len)
{
    if(out) *out = NULL;
    if(out_len) *out_len = 0;
    if(s == NULL)
        return -1;

    /* Render the remaining active region (optionally healed) as the final
     * segment, after the last committed sync point. */
    if(stream_render(s, &s->tail, s->accum.data + s->anchor,
                     s->accum.size - s->anchor, s->heal) != 0)
        return -1;
    if(stream_emit(s, s->tail.data, s->tail.size) != 0)
        return -1;

    s->anchor = s->accum.size;
    s->emitted = 1;
    s->finished = 1;

    if(out) *out = s->out.data;
    if(out_len) *out_len = s->out.size;
    return 0;
}
