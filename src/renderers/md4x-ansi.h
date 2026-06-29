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

#ifndef MD4X_ANSI_H
#define MD4X_ANSI_H

#include "md4x.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* If set, debug output from md_parse() is sent to stderr. */
#define MD_ANSI_FLAG_DEBUG 0x0001
#define MD_ANSI_FLAG_SKIP_UTF8_BOM 0x0002
#define MD_ANSI_FLAG_NO_COLOR 0x0004
#define MD_ANSI_FLAG_CODE_META 0x0008
#define MD_ANSI_FLAG_SHOW_URLS 0x0010
#define MD_ANSI_FLAG_SHOW_FRONTMATTER 0x0020
#define MD_ANSI_FLAG_HEAL 0x0100

    /* Render Markdown into ANSI terminal output.
     *
     * Produces text with ANSI escape codes for terminal display (bold, italic,
     * colors, etc.).
     *
     * Params input and input_size specify the Markdown input.
     * Callback process_output() gets called with chunks of ANSI output.
     * Param userdata is just propagated back to process_output() callback.
     * Param parser_flags are flags from md4x.h propagated to md_parse().
     * Param renderer_flags is bitmask of MD_ANSI_FLAG_xxxx.
     *
     * Returns -1 on error (if md_parse() fails.)
     * Returns 0 on success.
     */
    int md_ansi(const MD_CHAR *input, MD_SIZE input_size,
                void (*process_output)(const MD_CHAR *, MD_SIZE, void *),
                void *userdata, unsigned parser_flags, unsigned renderer_flags);

/* Table width modes for md_ansi_ex(). */
#define MD_ANSI_WIDTH_AUTO (-1) /* detect from $COLUMNS / terminal, else 80 */
#define MD_ANSI_WIDTH_INF 0     /* unlimited: size tables to content, no wrap */

    /* Like md_ansi(), but with an explicit table layout width.
     *
     * Param width controls table fit-to-width:
     *   > 0                  use this fixed width (columns).
     *   MD_ANSI_WIDTH_INF(0) unlimited width: tables are sized to content and
     *                        never shrunk/truncated.
     *   MD_ANSI_WIDTH_AUTO   auto-detect from $COLUMNS or the terminal
     *                        (falls back to 80).
     *
     * md_ansi() is equivalent to md_ansi_ex() with width = MD_ANSI_WIDTH_AUTO.
     */
    int md_ansi_ex(const MD_CHAR *input, MD_SIZE input_size,
                   void (*process_output)(const MD_CHAR *, MD_SIZE, void *),
                   void *userdata, unsigned parser_flags, unsigned renderer_flags,
                   int width);

    /* Resolve the auto layout width: from $COLUMNS, then the terminal
     * (TIOCGWINSZ), else 80. This is what MD_ANSI_WIDTH_AUTO uses internally;
     * exposed so callers (e.g. the streaming front-end) can pin a concrete
     * width once instead of re-detecting it per render. */
    int md_ansi_detect_width(void);

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* MD4X_ANSI_H */
