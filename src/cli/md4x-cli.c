/*
 * MD4X: Markdown parser for C
 * (http://github.com/unjs/md4x)
 *
 * Copyright (c) 2016-2024 Martin Mitáš
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "md4x-html.h"
#include "md4x-ast.h"
#include "md4x-ansi.h"
#include "md4x-text.h"
#include "md4x-markdown.h"
#include "md4x-heal.h"
#include "md4x-stream.h"
#include "cmdline.h"

#ifdef _WIN32
    #include <io.h>
    #define md4x_isatty(fd) _isatty(fd)
    #define md4x_fileno(f) _fileno(f)
#else
    #include <unistd.h>
    #define md4x_isatty(fd) isatty(fd)
    #define md4x_fileno(f) fileno(f)
#endif



/* Output format. */
typedef enum {
    FORMAT_HTML,
    FORMAT_TEXT,
    FORMAT_JSON,
    FORMAT_ANSI,
    FORMAT_MARKDOWN,
    FORMAT_HEAL
} OutputFormat;

static const char* format_name[] = { "html", "text", "json", "ansi", "markdown", "heal" };

/* Global options. */
static OutputFormat output_format = FORMAT_HTML;
static unsigned parser_flags = MD_DIALECT_ALL;
#ifndef MD4X_USE_ASCII
    static unsigned renderer_flags = MD_HTML_FLAG_DEBUG | MD_HTML_FLAG_SKIP_UTF8_BOM;
#else
    static unsigned renderer_flags = MD_HTML_FLAG_DEBUG;
#endif
static int want_fullhtml = 0;
static int want_heal = 0;
static int want_stat = 0;
static int want_replay_fuzz = 0;

/* ANSI output: color mode and table width. */
typedef enum { COLOR_AUTO, COLOR_ON, COLOR_OFF } ColorMode;
static ColorMode color_mode = COLOR_AUTO;
static int ansi_width = MD_ANSI_WIDTH_AUTO; /* >0 fixed, 0 inf, <0 auto */

/* Streaming (push) mode for ANSI output. */
static int want_stream = 0;
static int stream_chunk = 0;        /* push chunk size; 0 => default (undocumented override) */
static int want_stream_progressive = 0; /* drive md4x_stream_render(); reconstruct screen */

static const char* html_title = NULL;
static const char* css_path = NULL;


/*********************************
 ***  Simple grow-able buffer  ***
 *********************************/

/* We render to a memory buffer instead of directly outputting the rendered
 * documents, as this allows using this utility for evaluating performance
 * of MD4X (--stat option). This allows us to measure just time of the parser,
 * without the I/O.
 */

struct membuffer {
    char* data;
    size_t asize;
    size_t size;
};

static void
membuf_init(struct membuffer* buf, MD_SIZE new_asize)
{
    buf->size = 0;
    buf->asize = new_asize;
    buf->data = malloc(buf->asize);
    if(buf->data == NULL) {
        fprintf(stderr, "membuf_init: malloc() failed.\n");
        exit(1);
    }
}

static void
membuf_fini(struct membuffer* buf)
{
    if(buf->data)
        free(buf->data);
}

static void
membuf_grow(struct membuffer* buf, size_t new_asize)
{
    buf->data = realloc(buf->data, new_asize);
    if(buf->data == NULL) {
        fprintf(stderr, "membuf_grow: realloc() failed.\n");
        exit(1);
    }
    buf->asize = new_asize;
}

static void
membuf_append(struct membuffer* buf, const char* data, MD_SIZE size)
{
    if(buf->asize < buf->size + size)
        membuf_grow(buf, buf->size + buf->size / 2 + size);
    memcpy(buf->data + buf->size, data, size);
    buf->size += size;
}


/**********************
 ***  Main program  ***
 **********************/

static void
process_output(const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    membuf_append((struct membuffer*) userdata, text, size);
}

static size_t
count_nl(const char* s, size_t n)
{
    size_t i, c = 0;
    for(i = 0; i < n; i++)
        if(s[i] == '\n') c++;
    return c;
}

/* Live progressive ANSI rendering. Reads input incrementally and drives
 * md4x_stream_render(). When `out` is a terminal, each update is applied with
 * cursor control: move up `backtrack` lines, clear to end of screen, print the
 * new content. The on-screen active row count is tracked so we never move up
 * past it. When `out` is not a terminal, the updates are applied to a virtual
 * screen and the final result is written (deterministic; matches one-shot). */
static int
process_ansi_progressive(FILE* in, FILE* out)
{
    MD4X_STREAM_OPTS opts;
    MD4X_STREAM* s;
    struct membuffer scr = {0};
    char rbuf[8192];
    size_t rd, want;
    int tty = md4x_isatty(md4x_fileno(out));
    int use_color;
    unsigned a_flags = 0;
    size_t active_rows = 0;
    int ret = 0;

#ifndef MD4X_USE_ASCII
    a_flags |= MD_ANSI_FLAG_SKIP_UTF8_BOM;
#endif
    if(color_mode == COLOR_ON)
        use_color = 1;
    else if(color_mode == COLOR_OFF)
        use_color = 0;
    else
        use_color = tty;
    if(!use_color)
        a_flags |= MD_ANSI_FLAG_NO_COLOR;

    memset(&opts, 0, sizeof(opts));
    opts.parser_flags = parser_flags;
    opts.renderer_flags = a_flags;
    opts.width = ansi_width;
    opts.heal = want_heal ? 1 : 0;

    s = md4x_stream_create(&opts);
    if(s == NULL)
        return -1;
    if(!tty)
        membuf_init(&scr, 8192);

    /* Read size: honor --stream-chunk (for tests), else a full buffer. */
    want = (stream_chunk > 0 && (size_t) stream_chunk < sizeof(rbuf))
         ? (size_t) stream_chunk : sizeof(rbuf);

    while((rd = fread(rbuf, 1, want, in)) > 0) {
        MD4X_STREAM_UPDATE upd;
        if(md4x_stream_render(s, rbuf, rd, &upd) != 0) { ret = -1; break; }

        if(tty) {
            if(upd.backtrack > 0) {
                size_t b = (upd.backtrack > active_rows) ? active_rows : upd.backtrack;
                char esc[32];
                int m = snprintf(esc, sizeof(esc), "\033[%uA\r\033[J", (unsigned) b);
                fwrite(esc, 1, (size_t) m, out);
                active_rows -= b;
            }
            if(upd.content_len > 0)
                fwrite(upd.content, 1, upd.content_len, out);
            fflush(out);
            active_rows += count_nl(upd.content, upd.content_len);
            active_rows = (upd.freeze >= active_rows) ? 0 : active_rows - upd.freeze;
        } else {
            /* Reconstruct the virtual screen: drop `backtrack` trailing lines,
             * then append the new content. */
            size_t pos = scr.size, k;
            for(k = 0; k < upd.backtrack && pos > 0; k++) {
                pos--;
                while(pos > 0 && scr.data[pos - 1] != '\n') pos--;
            }
            scr.size = pos;
            if(upd.content_len > 0)
                membuf_append(&scr, upd.content, (MD_SIZE) upd.content_len);
        }
    }

    if(!tty && ret == 0)
        fwrite(scr.data, 1, scr.size, out);

    md4x_stream_destroy(s);
    membuf_fini(&scr);
    return ret;
}

static int
process_file(const char* in_path, FILE* in, FILE* out)
{
    size_t n;
    struct membuffer buf_in = {0};
    struct membuffer buf_out = {0};
    int ret = -1;
    clock_t t0, t1;
    unsigned p_flags = parser_flags;
    unsigned r_flags = renderer_flags;

    /* Live progressive ANSI mode reads input incrementally (it does not need
     * the whole document up front), so handle it before buffering input. */
    if(output_format == FORMAT_ANSI && want_stream_progressive)
        return process_ansi_progressive(in, out);

    membuf_init(&buf_in, 32 * 1024);

    /* Read the input file into a buffer. */
    while(1) {
        if(buf_in.size >= buf_in.asize)
            membuf_grow(&buf_in, buf_in.asize + buf_in.asize / 2);

        n = fread(buf_in.data + buf_in.size, 1, buf_in.asize - buf_in.size, in);
        if(n == 0)
            break;
        buf_in.size += n;
    }

    /* Input size is good estimation of output size. Add some more reserve to
     * deal with the HTML header/footer and tags. */
    membuf_init(&buf_out, (MD_SIZE)(buf_in.size + buf_in.size/8 + 64));

    /* Special mode for reproduce test case found with fuzzing a tool.
     * We assume file format as produced by test/fuzzers/fuzz-mdhtml.c. */
    if(want_replay_fuzz) {
        if(buf_in.size < 2 * sizeof(unsigned)) {
            fprintf(stderr, "File %s isn't valid fuzz test case.\n", in_path);
            ret = -1;
            goto out;
        }

        /* Override parser and renderer flags with those from the test case. */
        memcpy(&p_flags, buf_in.data, sizeof(unsigned));
        memcpy(&r_flags, buf_in.data + sizeof(unsigned), sizeof(unsigned));

        /* And get rid of them from the text input to the parser. */
        memmove(buf_in.data, buf_in.data + 2 * sizeof(unsigned),
                    buf_in.size - 2 * sizeof(unsigned));
        buf_in.size -= 2 * sizeof(unsigned);

        /* Zero the tail we have moved the contents from.
         * It helps in debugging if make it actually a zero-terminated string. */
        memset(buf_in.data + buf_in.size, 0, 2 * sizeof(unsigned));
    }

    /* Apply heal flag to renderer flags if requested (HTML uses r_flags). */
    if(want_heal)
        r_flags |= MD_HTML_FLAG_HEAL;

    /* Parse and render the document. */
    t0 = clock();

    switch(output_format) {
        case FORMAT_HTML: {
            unsigned html_flags = r_flags;
            MD_HTML_OPTS html_opts = { NULL, NULL };
            const MD_HTML_OPTS* opts_ptr = NULL;

            if(want_fullhtml) {
                html_flags |= MD_HTML_FLAG_FULL_HTML;
                html_opts.title = html_title;
                html_opts.css_url = css_path;
                opts_ptr = &html_opts;
            }

            ret = md_html_ex(buf_in.data, (MD_SIZE)buf_in.size, process_output,
                        (void*) &buf_out, p_flags, html_flags, opts_ptr);
            break;
        }
        case FORMAT_JSON: {
            unsigned j_flags = MD_AST_FLAG_DEBUG;
#ifndef MD4X_USE_ASCII
            j_flags |= MD_AST_FLAG_SKIP_UTF8_BOM;
#endif
            if(want_heal) j_flags |= MD_AST_FLAG_HEAL;
            ret = md_ast(buf_in.data, (MD_SIZE)buf_in.size, process_output,
                        (void*) &buf_out, p_flags, j_flags);
            break;
        }
        case FORMAT_ANSI: {
            unsigned a_flags = MD_ANSI_FLAG_DEBUG;
            int use_color;
#ifndef MD4X_USE_ASCII
            a_flags |= MD_ANSI_FLAG_SKIP_UTF8_BOM;
#endif
            if(want_heal) a_flags |= MD_ANSI_FLAG_HEAL;

            /* Resolve color: auto = enabled only when output is a terminal. */
            if(color_mode == COLOR_ON)
                use_color = 1;
            else if(color_mode == COLOR_OFF)
                use_color = 0;
            else
                use_color = md4x_isatty(md4x_fileno(out));
            if(!use_color)
                a_flags |= MD_ANSI_FLAG_NO_COLOR;

            if(want_stream) {
                /* Push the input through the streaming context in chunks,
                 * collecting committed output, then the final remainder. */
                MD4X_STREAM_OPTS opts;
                MD4X_STREAM* s;
                const char* o;
                size_t olen, off;
                size_t chunk = (stream_chunk > 0) ? (size_t) stream_chunk : 16;

                memset(&opts, 0, sizeof(opts));
                opts.parser_flags = p_flags;
                opts.renderer_flags = a_flags & ~(unsigned) MD_ANSI_FLAG_DEBUG;
                opts.width = ansi_width;
                opts.heal = want_heal ? 1 : 0;

                s = md4x_stream_create(&opts);
                if(s == NULL) { ret = -1; break; }

                ret = 0;
                for(off = 0; off < buf_in.size; off += chunk) {
                    size_t clen = buf_in.size - off;
                    if(clen > chunk) clen = chunk;
                    if(md4x_stream_push(s, buf_in.data + off, clen, &o, &olen) != 0) {
                        ret = -1;
                        break;
                    }
                    if(olen > 0) membuf_append(&buf_out, o, (MD_SIZE) olen);
                }
                if(ret == 0) {
                    if(md4x_stream_finish(s, &o, &olen) != 0)
                        ret = -1;
                    else if(olen > 0)
                        membuf_append(&buf_out, o, (MD_SIZE) olen);
                }
                md4x_stream_destroy(s);
                break;
            }

            ret = md_ansi_ex(buf_in.data, (MD_SIZE)buf_in.size, process_output,
                        (void*) &buf_out, p_flags, a_flags, ansi_width);
            break;
        }
        case FORMAT_TEXT: {
            unsigned t_flags = MD_TEXT_FLAG_DEBUG;
#ifndef MD4X_USE_ASCII
            t_flags |= MD_TEXT_FLAG_SKIP_UTF8_BOM;
#endif
            if(want_heal) t_flags |= MD_TEXT_FLAG_HEAL;
            ret = md_text(buf_in.data, (MD_SIZE)buf_in.size, process_output,
                        (void*) &buf_out, p_flags, t_flags);
            break;
        }
        case FORMAT_MARKDOWN: {
            unsigned pm_flags = MD_MARKDOWN_FLAG_DEBUG;
#ifndef MD4X_USE_ASCII
            pm_flags |= MD_MARKDOWN_FLAG_SKIP_UTF8_BOM;
#endif
            if(want_heal) pm_flags |= MD_MARKDOWN_FLAG_HEAL;
            ret = md_markdown(buf_in.data, (MD_SIZE)buf_in.size, process_output,
                        (void*) &buf_out, p_flags, pm_flags);
            break;
        }
        case FORMAT_HEAL: {
            ret = md_heal(buf_in.data, (MD_SIZE)buf_in.size, process_output,
                        (void*) &buf_out);
            break;
        }
    }

    t1 = clock();
    if(ret != 0) {
        fprintf(stderr, "Parsing failed.\n");
        goto out;
    }

    fwrite(buf_out.data, 1, buf_out.size, out);

    if(want_stat) {
        if(t0 != (clock_t)-1  &&  t1 != (clock_t)-1) {
            double elapsed = (double)(t1 - t0) / CLOCKS_PER_SEC;
            if (elapsed < 1)
                fprintf(stderr, "Time spent on parsing: %7.2f ms.\n", elapsed*1e3);
            else
                fprintf(stderr, "Time spent on parsing: %6.3f s.\n", elapsed);
        }
    }

    /* Success if we have reached here. */
    ret = 0;

out:
    membuf_fini(&buf_in);
    membuf_fini(&buf_out);

    return ret;
}


static const CMDLINE_OPTION cmdline_options[] = {
    { 'o', "output",                        'o', CMDLINE_OPTFLAG_REQUIREDARG },
    { 'f', "full-html",                     'f', 0 },
    {  0,  "heal",                          '4', 0 },
    { 's', "stat",                          's', 0 },
    { 'h', "help",                          'h', 0 },
    { 'v', "version",                       'v', 0 },

    { 't', "format",                        '3', CMDLINE_OPTFLAG_REQUIREDARG },

    {  0,  "color",                         '5', CMDLINE_OPTFLAG_REQUIREDARG },
    {  0,  "width",                         '6', CMDLINE_OPTFLAG_REQUIREDARG },
    {  0,  "stream",                        '7', 0 },
    {  0,  "stream-progressive",            '9', 0 },

    /* Undocumented: override the streaming push chunk size (for tests). */
    {  0,  "stream-chunk",                  '8', CMDLINE_OPTFLAG_REQUIREDARG },

    {  0,  "html-title",                    '1', CMDLINE_OPTFLAG_REQUIREDARG },
    {  0,  "html-css",                      '2', CMDLINE_OPTFLAG_REQUIREDARG },

    /* Undocumented option for replaying test cases from fuzzers. */
    {  0,  "replay-fuzz",                   'r', 0 },

    {  0,  NULL,                             0,  0 }
};

static void
usage(void)
{
    printf(
        "Usage: md4x [OPTION]... [FILE]\n"
        "Convert input FILE (or standard input) in Markdown format.\n"
        "\n"
        "General options:\n"
        "  -o  --output=FILE    Output file (default is standard output)\n"
        "  -t, --format=FORMAT  Output format: html (default), text, json, ansi, markdown, heal\n"
        "      --heal           Heal incomplete markdown before rendering\n"
        "  -s, --stat           Measure time of input parsing\n"
        "  -h, --help           Display this help and exit\n"
        "  -v, --version        Display version and exit\n"
        "\n"
        "ANSI output options (--format=ansi):\n"
        "      --color=MODE     Color output: auto (default), on, off\n"
        "      --width=WIDTH    Table width: auto (default), inf, or a column count\n"
        "      --stream         Render incrementally (push mode); emits stable output as it arrives\n"
        "      --stream-progressive  Live progressive render; updates the active region in place on a terminal\n"
        "\n"
        "HTML output options:\n"
        "  -f, --full-html      Generate full HTML document, including header\n"
        "      --html-title=TITLE Sets the title of the document\n"
        "      --html-css=URL   In full HTML mode add a css link\n"
        "\n"
    );
}

static void
version(void)
{
    printf("%d.%d.%d\n", MD_VERSION_MAJOR, MD_VERSION_MINOR, MD_VERSION_RELEASE);
}

static const char* input_path = NULL;
static const char* output_path = NULL;

static int
cmdline_callback(int opt, char const* value, void* data)
{
    (void) data;   /* unused parameter */

    switch(opt) {
        case 0:
            if(input_path) {
                fprintf(stderr, "Too many arguments. Only one input file can be specified.\n");
                fprintf(stderr, "Use --help for more info.\n");
                exit(1);
            }
            input_path = value;
            break;

        case 'o':   output_path = value; break;
        case 'f':   want_fullhtml = 1; break;
        case '4':   want_heal = 1; break;
        case 's':   want_stat = 1; break;
        case 'r':   want_replay_fuzz = 1; break;
        case 'h':   usage(); exit(0); break;
        case 'v':   version(); exit(0); break;

        case '3':
            if(strcmp(value, "html") == 0)
                output_format = FORMAT_HTML;
            else if(strcmp(value, "text") == 0)
                output_format = FORMAT_TEXT;
            else if(strcmp(value, "json") == 0)
                output_format = FORMAT_JSON;
            else if(strcmp(value, "ansi") == 0)
                output_format = FORMAT_ANSI;
            else if(strcmp(value, "markdown") == 0)
                output_format = FORMAT_MARKDOWN;
            else if(strcmp(value, "heal") == 0)
                output_format = FORMAT_HEAL;
            else {
                fprintf(stderr, "Unknown format: %s\n", value);
                fprintf(stderr, "Supported formats: html, text, json, ansi, markdown, heal\n");
                exit(1);
            }
            break;

        case '5':   /* --color=auto|on|off */
            if(strcmp(value, "auto") == 0)
                color_mode = COLOR_AUTO;
            else if(strcmp(value, "on") == 0 || strcmp(value, "always") == 0)
                color_mode = COLOR_ON;
            else if(strcmp(value, "off") == 0 || strcmp(value, "never") == 0)
                color_mode = COLOR_OFF;
            else {
                fprintf(stderr, "Invalid --color value: %s (use auto, on, or off)\n", value);
                exit(1);
            }
            break;

        case '7':   want_stream = 1; break;
        case '9':   want_stream = 1; want_stream_progressive = 1; break;
        case '8':   /* --stream-chunk=<n> (undocumented) */
            stream_chunk = atoi(value);
            if(stream_chunk < 1) {
                fprintf(stderr, "Invalid --stream-chunk value: %s\n", value);
                exit(1);
            }
            break;

        case '6':   /* --width=auto|inf|0|<n> */
            if(strcmp(value, "auto") == 0) {
                ansi_width = MD_ANSI_WIDTH_AUTO;
            } else if(strcmp(value, "inf") == 0) {
                ansi_width = MD_ANSI_WIDTH_INF;
            } else {
                char* end = NULL;
                long w = strtol(value, &end, 10);
                if(end == value || *end != '\0' || w < 0 || w > 100000) {
                    fprintf(stderr, "Invalid --width value: %s (use auto, inf, 0, or a column count)\n", value);
                    exit(1);
                }
                /* 0 == inf (unlimited), as documented. */
                ansi_width = (int) w;
            }
            break;

        case '1':   html_title = value; break;
        case '2':   css_path = value; break;

        default:
            fprintf(stderr, "Illegal option: %s\n", value);
            fprintf(stderr, "Use --help for more info.\n");
            exit(1);
            break;
    }

    return 0;
}

int
main(int argc, char** argv)
{
    FILE* in = stdin;
    FILE* out = stdout;
    int ret = 0;

    if(cmdline_read(cmdline_options, argc, argv, cmdline_callback, NULL) != 0) {
        usage();
        exit(1);
    }

    if(input_path != NULL && strcmp(input_path, "-") != 0) {
        in = fopen(input_path, "rb");
        if(in == NULL) {
            fprintf(stderr, "Cannot open %s.\n", input_path);
            exit(1);
        }
    }
    if(output_path != NULL && strcmp(output_path, "-") != 0) {
        out = fopen(output_path, "wt");
        if(out == NULL) {
            fprintf(stderr, "Cannot open %s.\n", output_path);
            exit(1);
        }
    }

    ret = process_file((input_path != NULL) ? input_path : "<stdin>", in, out);
    if(in != stdin)
        fclose(in);
    if(out != stdout)
        fclose(out);

    return ret;
}
