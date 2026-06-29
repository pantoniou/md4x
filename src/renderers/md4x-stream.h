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

#ifndef MD4X_STREAM_H
#define MD4X_STREAM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Push/streaming front-end for the ANSI renderer.
     *
     * The Markdown parser is one-shot (it needs the whole document), so this is
     * a layer over md_ansi_ex(). It accumulates pushed input and commits output
     * up to the last "safe sync point": a blank line at which all block
     * containers are closed (top level, not inside a fenced code block, followed
     * by a non-list block). At such a point the renderer is in its initial state,
     * so the text after it renders standalone identically to the tail of a full
     * render -- which lets the context re-render only the active region since the
     * last sync point (not the whole document) and emit the stable prefix.
     *
     * Caveat: a CommonMark link reference definition appearing later can in
     * principle change how an earlier link rendered; such retroactive changes are
     * not applied to already-committed output.
     *
     * Typical use (live terminal output):
     *
     *     MD4X_STREAM* s = md4x_stream_create(NULL);
     *     const char* out; size_t n;
     *     while (read_chunk(&buf, &len))
     *         if (md4x_stream_push(s, buf, len, &out, &n) == 0 && n)
     *             fwrite(out, 1, n, stdout);   // committed, append-only
     *     md4x_stream_finish(s, &out, &n);
     *     fwrite(out, 1, n, stdout);           // final remainder
     *     md4x_stream_destroy(s);
     *
     * For a redrawn "active region", call md4x_stream_preview() between pushes to
     * get a healed render of the not-yet-committed tail.
     *
     * Returned pointers are owned by the context and remain valid only until the
     * next call on that context (push/preview/finish/destroy).
     */

    typedef struct MD4X_STREAM MD4X_STREAM;

    typedef struct MD4X_STREAM_OPTS
    {
        unsigned parser_flags;   /* parser flags (0 => MD_DIALECT_ALL) */
        unsigned renderer_flags; /* MD_ANSI_FLAG_* (e.g. NO_COLOR); HEAL is managed internally */
        int width;              /* MD_ANSI_WIDTH_AUTO / MD_ANSI_WIDTH_INF / fixed columns */
        int heal;               /* non-zero: preview()/finish() close dangling markers */
    } MD4X_STREAM_OPTS;

    /* Create a streaming context. Pass NULL for defaults (MD_DIALECT_ALL parser
     * flags, no renderer flags, auto width, healing enabled). The width is
     * resolved once here, so a later terminal resize cannot destabilize already
     * committed output. Returns NULL on allocation failure. */
    MD4X_STREAM* md4x_stream_create(const MD4X_STREAM_OPTS* opts);

    /* Destroy a context and free all its buffers. */
    void md4x_stream_destroy(MD4X_STREAM* s);

    /* Append input and emit newly-committed (stable) output.
     *
     * *out / *out_len receive the slice of output that is now safe to render
     * (complete lines whose bytes are identical to the previous render). The
     * slice may be empty. It points into context-owned memory valid until the
     * next call on this context. Returns 0 on success, -1 on allocation failure. */
    int md4x_stream_push(MD4X_STREAM* s, const char* chunk, size_t len,
                         const char** out, size_t* out_len);

    /* Healed render of the not-yet-committed tail (the "active region"), for a
     * caller that redraws it each frame. Does not advance the commit point.
     * *out / *out_len are context-owned (valid until the next call). Returns 0
     * on success, -1 on allocation failure. */
    int md4x_stream_preview(MD4X_STREAM* s, const char** out, size_t* out_len);

    /* Final flush: the output following the last committed prefix, optionally
     * healed (per opts.heal). After finish(), do not call push() again on this
     * context. *out / *out_len are context-owned. Returns 0 / -1. */
    int md4x_stream_finish(MD4X_STREAM* s, const char** out, size_t* out_len);

    /* Progressive line-diff update produced by md4x_stream_render().
     *
     * The active region (everything below the last committed line) is re-rendered
     * and diffed against the previous render line by line. Apply it to a virtual
     * terminal as: move the cursor up `backtrack` lines, clear to end of screen,
     * then print `content`. Usually only the last line or two change, so
     * `backtrack` is small; a table reflow may change the whole active region.
     *
     *   backtrack    Number of trailing active-region lines to erase upward.
     *   content      Replacement text to print at that point (newline-terminated
     *                lines); context-owned, valid until the next call.
     *   freeze       After applying, this many lines at the TOP of the active
     *                region have reached a safe sync point and are now permanent
     *                (they will never be backtracked into again). Informational —
     *                future updates' `backtrack` never exceeds the still-mutable
     *                line count.
     */
    typedef struct MD4X_STREAM_UPDATE {
        size_t backtrack;
        const char* content;
        size_t content_len;
        size_t freeze;
    } MD4X_STREAM_UPDATE;

    /* Append input and produce a progressive update for the active region (the
     * region after the last safe sync point). The active region is rendered
     * unhealed, so what is displayed matches the final committed output and a
     * line only changes when its source does. Returns 0 / -1. */
    int md4x_stream_render(MD4X_STREAM* s, const char* chunk, size_t len,
                           MD4X_STREAM_UPDATE* upd);

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* MD4X_STREAM_H */
