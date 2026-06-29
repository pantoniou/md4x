# Renderers

## HTML Renderer API (`md4x-html.h`)

Convenience library that wraps `md_parse()` and produces HTML output:

```c
int md_html(const MD_CHAR* input, MD_SIZE input_size,
            void (*process_output)(const MD_CHAR*, MD_SIZE, void*),
            void* userdata, unsigned parser_flags, unsigned renderer_flags);
```

Only `<body>` contents are generated. Frontmatter blocks are suppressed from output.

Extended API with full-HTML document generation:

```c
typedef struct MD_HTML_OPTS {
    const char* title;      /* Document title override (NULL = use frontmatter) */
    const char* css_url;    /* CSS stylesheet URL (NULL = omit) */
} MD_HTML_OPTS;

int md_html_ex(const MD_CHAR* input, MD_SIZE input_size,
               void (*process_output)(const MD_CHAR*, MD_SIZE, void*),
               void* userdata, unsigned parser_flags, unsigned renderer_flags,
               const MD_HTML_OPTS* opts);
```

When `MD_HTML_FLAG_FULL_HTML` is set, `md_html_ex()` generates a complete HTML document (`<!DOCTYPE html>`, `<head>`, `<body>`). If YAML frontmatter exists, `title` and `description` fields are used in `<head>`. The `opts->title` overrides the frontmatter title. `opts` may be NULL.

### Renderer Flags (`MD_HTML_FLAG_*`)

| Flag                             | Value    | Description                                         |
| -------------------------------- | -------- | --------------------------------------------------- |
| `MD_HTML_FLAG_DEBUG`             | `0x0001` | Send debug output from `md_parse()` to stderr       |
| `MD_HTML_FLAG_VERBATIM_ENTITIES` | `0x0002` | Do not translate HTML entities                      |
| `MD_HTML_FLAG_SKIP_UTF8_BOM`     | `0x0004` | Skip UTF-8 BOM at input start                       |
| `MD_HTML_FLAG_FULL_HTML`         | `0x0008` | Generate full HTML document (requires `md_html_ex`) |

### Rendering Details

- Frontmatter blocks are suppressed (not rendered in HTML output)
- Wiki links render as `<x-wikilink>` tags
- LaTeX math renders as `<x-equation>` tags
- Task lists render with `<input type="checkbox">` elements
- Table cells get `align` attribute when alignment is specified
- URL attributes are percent-encoded; HTML content is entity-escaped
- Alerts render as `<blockquote class="alert alert-{type}">` (type lowercased in class)

## Shared Property Parser (`md4x-props.h`)

Header-only utility for parsing component property strings (`{key="value" bool #id .class :bind='json'}`). Used by both JSON and HTML renderers.

```c
#include "md4x-props.h"

MD_PARSED_PROPS parsed;
md_parse_props(raw, size, &parsed);
```

**Parsed output (`MD_PARSED_PROPS`):**

| Field                     | Type                         | Description                                    |
| ------------------------- | ---------------------------- | ---------------------------------------------- |
| `props[32]`               | `MD_PROP[]`                  | Parsed props (key/value pairs, booleans, bind) |
| `n_props`                 | `int`                        | Number of parsed props                         |
| `id` / `id_size`          | `const MD_CHAR*` / `MD_SIZE` | `#id` shorthand (last wins)                    |
| `class_buf` / `class_len` | `MD_CHAR[512]` / `MD_SIZE`   | Merged `.class` values (space-separated)       |

**Prop types (`MD_PROP_TYPE`):**

| Type              | Syntax                                    | Description              |
| ----------------- | ----------------------------------------- | ------------------------ |
| `MD_PROP_STRING`  | `key="value"`, `key='value'`, `key=value` | String prop              |
| `MD_PROP_BOOLEAN` | `flag`                                    | Boolean prop (bare word) |
| `MD_PROP_BIND`    | `:key='json'`                             | JSON passthrough         |

All `key`/`value` pointers are zero-copy references into the original raw string (not null-terminated — use `*_size` fields).

## AST Renderer API (`md4x-ast.h`)

Renders Markdown into a Comark AST (array-based JSON format):

```c
int md_ast(const MD_CHAR* input, MD_SIZE input_size,
            void (*process_output)(const MD_CHAR*, MD_SIZE, void*),
            void* userdata, unsigned parser_flags, unsigned renderer_flags);
```

Produces `{"nodes":[...],"frontmatter":{...},"meta":{}}` where each node is either a plain JSON string (text) or a tuple array `["tag", {props}, ...children]`. Frontmatter YAML is parsed into the top-level `frontmatter` object (not included in `nodes`). HTML comments are represented as `[null, {}, "comment body"]`.

**Internal architecture:** Unlike the streaming HTML/ANSI renderers, the AST renderer builds an in-memory tree of `JSON_NODE` structs during parsing, then serializes the tree to JSON. Each node has a `detail` union for type-specific data (code block info, link href, component props, etc.). Nodes with `tag_is_dynamic = 1` are user-defined components — their tag name is heap-allocated and they use the `detail.component` union member exclusively. All dispatch on `node->tag` (in `json_node_free`, `json_write_props`, `json_serialize_node`) must check `tag_is_dynamic` first to avoid union misinterpretation when a component name collides with a built-in tag.

### AST Renderer Flags (`MD_AST_FLAG_*`)

| Flag                        | Value    | Description                                   |
| --------------------------- | -------- | --------------------------------------------- |
| `MD_AST_FLAG_DEBUG`         | `0x0001` | Send debug output from `md_parse()` to stderr |
| `MD_AST_FLAG_SKIP_UTF8_BOM` | `0x0002` | Skip UTF-8 BOM at input start                 |

## ANSI Renderer API (`md4x-ansi.h`)

Renders Markdown into ANSI terminal output with escape codes for styling:

```c
int md_ansi(const MD_CHAR* input, MD_SIZE input_size,
            void (*process_output)(const MD_CHAR*, MD_SIZE, void*),
            void* userdata, unsigned parser_flags, unsigned renderer_flags);
```

Extended API with an explicit layout/wrap width:

```c
int md_ansi_ex(const MD_CHAR* input, MD_SIZE input_size,
               void (*process_output)(const MD_CHAR*, MD_SIZE, void*),
               void* userdata, unsigned parser_flags, unsigned renderer_flags,
               int width);
```

The `width` controls table layout and prose word-wrapping:

| `width`                  | Behavior                                                       |
| ------------------------ | -------------------------------------------------------------- |
| `> 0`                    | Fixed width (columns).                                         |
| `MD_ANSI_WIDTH_INF` (`0`) | Unlimited: size tables to content, no wrapping.               |
| `MD_ANSI_WIDTH_AUTO` (`-1`) | Auto-detect from `$COLUMNS` / terminal (`TIOCGWINSZ`), else 80. |

`md_ansi()` is equivalent to `md_ansi_ex()` with `width = MD_ANSI_WIDTH_AUTO`.

### Renderer Flags (`MD_ANSI_FLAG_*`)

| Flag                            | Value    | Description                                          |
| ------------------------------- | -------- | ---------------------------------------------------- |
| `MD_ANSI_FLAG_DEBUG`            | `0x0001` | Send debug output from `md_parse()` to stderr        |
| `MD_ANSI_FLAG_SKIP_UTF8_BOM`    | `0x0002` | Skip UTF-8 BOM at input start                        |
| `MD_ANSI_FLAG_NO_COLOR`         | `0x0004` | Suppress ANSI escape codes (plain text output)       |
| `MD_ANSI_FLAG_CODE_META`        | `0x0008` | Append code block metadata after null byte           |
| `MD_ANSI_FLAG_SHOW_URLS`        | `0x0010` | Show link URLs after link text (default: OSC 8 only) |
| `MD_ANSI_FLAG_SHOW_FRONTMATTER` | `0x0020` | Show frontmatter as dim text (default: suppressed)   |

### Rendering Details

- Headings: bold magenta (`\033[1;35m`)
- Bold/strong: bold (`\033[1m`)
- Italic/emphasis: italic (`\033[3m`)
- Underline: underline (`\033[4m`)
- Strikethrough: strikethrough (`\033[9m`)
- Inline code: cyan (`\033[36m`)
- Code blocks: dim (`\033[2m`) with 2-space indent; left preformatted (never wrapped)
- Links: underline blue (`\033[4;34m`) with OSC 8 clickable hyperlinks
- Blockquotes: dim vertical bar prefix (`│`)
- Horizontal rules: box-drawing line (`────────`)
- Lists: dim bullet/number prefix with nesting indentation
- Task lists: `[x]`/`[ ]` with green for checked items
- Images: `[image: alt]` in dim
- Alerts: colored thick left bar (`▌`) with type-specific colors (note/info=blue, tip/success=green, important=magenta, warning=yellow, caution/danger=red), bold type label on first line
- Components: cyan for generic; alert-like components (`::note`, `::tip`, `::important`, `::warning`, `::caution`) and `::alert{type="..."}` render with the same colored bar style as alerts
- Frontmatter: suppressed by default (enable with `MD_ANSI_FLAG_SHOW_FRONTMATTER` for dim text output)
- Raw HTML: stripped (not rendered)
- Entities resolved to UTF-8 characters

#### Layout (tables, wrapping, margins)

Modeled on the [glow](https://github.com/charmbracelet/glow) / charmbracelet lipgloss renderer:

- **Tables**: laid out (not tab-separated). Columns are sized to content with Unicode box separators (`│ ┼ ─`), a single header separator, per-column alignment (left/center/right), and a 1-space cell padding. Columns expand to fill the target width, or shrink with word-wrapped cells when too wide.
- **Word-wrapping**: headings, paragraphs, blockquotes, list items, and table cells are word-wrapped to the target width. Soft breaks reflow to spaces; hard breaks are preserved. `MD_ANSI_WIDTH_INF` disables wrapping.
- **Margins**: a symmetric 2-column document margin is applied to every line. Blockquotes render as `  │ ` and nest as `  │ │ `.
- **Display widths**: a Markus-Kuhn-style `wcwidth` table is used — zero-width combining/format marks, East Asian Wide/Fullwidth and most emoji count as 2 columns, everything else (including Greek) as 1. ANSI escape sequences are skipped when measuring. Grapheme clustering is not performed, so ZWJ emoji sequences may measure wider than they render.

Uses a streaming renderer pattern (like the HTML renderer), with per-line buffering only for word-wrapping; tables buffer their cells to lay out columns.

## Streaming / Push API (`md4x-stream.h`)

A push-mode front-end for the ANSI renderer, aimed at live terminal output (e.g. streaming LLM responses). Instead of one callback over the whole document, the caller creates a context and pushes input chunks; each push returns the output that is now safe to commit.

```c
typedef struct MD4X_STREAM MD4X_STREAM;

typedef struct MD4X_STREAM_OPTS {
    unsigned parser_flags;   /* 0 => MD_DIALECT_ALL */
    unsigned renderer_flags; /* MD_ANSI_FLAG_* (e.g. NO_COLOR); HEAL is managed internally */
    int      width;          /* MD_ANSI_WIDTH_AUTO / _INF / fixed columns */
    int      heal;           /* non-zero: preview()/finish() close dangling markers */
} MD4X_STREAM_OPTS;

MD4X_STREAM* md4x_stream_create(const MD4X_STREAM_OPTS* opts);  /* NULL = defaults */
void         md4x_stream_destroy(MD4X_STREAM* s);

int md4x_stream_push(MD4X_STREAM* s, const char* chunk, size_t len,
                     const char** out, size_t* out_len);   /* committed output */
int md4x_stream_preview(MD4X_STREAM* s, const char** out, size_t* out_len); /* healed active region */
int md4x_stream_finish(MD4X_STREAM* s, const char** out, size_t* out_len);  /* final remainder */
```

All functions return 0 / -1 (allocation failure). Returned pointers are owned by the context and remain valid only until the next call on it.

### How it works

The Markdown parser is one-shot, so the context renders via `md_ansi_ex()`. The key is the **safe sync point**: a blank line at which all block containers are closed — top level, not inside a fenced code block, and followed by a complete next line that starts a new block at column 0 and is **not** a list item. At such a point the renderer is in its initial state, so the text after it renders standalone byte-for-byte identically to the tail of a full render.

- **`push`** appends the chunk, re-renders only the **active region** (since the last sync point), advances the anchor to the furthest new safe sync point, and commits the segment between the old and new anchor. The segment is verified (its standalone render must be a true byte-prefix of the active-region render) before committing, so loose lists, tables, setext headings, etc. are never mis-committed. Committed segments are joined by the inter-block newline separator.
- **`preview`** returns a healed render of the active region (for redrawing the in-progress tail each frame).
- **`finish`** renders and returns the remaining active region (optionally healed to close dangling markers from a truncated stream).

Because only the active region is re-rendered (not the whole document), work stays bounded as the anchor advances.

### Progressive updates (`md4x_stream_render`)

For a terminal that updates the active region in place, `md4x_stream_render()` returns a line-diff instead of append-only output:

```c
typedef struct MD4X_STREAM_UPDATE {
    size_t backtrack;     /* trailing active-region lines to erase upward */
    const char* content;  /* replacement text to print (newline-terminated lines) */
    size_t content_len;
    size_t freeze;        /* lines at the top of the active region now permanent */
} MD4X_STREAM_UPDATE;

int md4x_stream_render(MD4X_STREAM* s, const char* chunk, size_t len, MD4X_STREAM_UPDATE* upd);
```

Each call appends input, re-renders the active region **unhealed** (so what is displayed equals the committed truth), and diffs it against the previous render line by line. Apply the update to a virtual terminal as: move the cursor up `backtrack` lines, clear to end of screen, then print `content`. Usually only the last line or two change (small `backtrack`); a table reflow may change the whole active region. `freeze` reports how many top lines just reached a safe sync point and are now permanent (future `backtrack` never exceeds the still-mutable line count). Applying every update to a virtual screen reconstructs exactly the one-shot render.

**Guarantee / caveat:** with healing off, the concatenation of all committed pushes plus `finish` is byte-identical to a one-shot `md_ansi`/`md_ansi_ex` render of the same input. The one theoretical exception is a CommonMark link reference definition appearing later in the stream, which can change how an earlier link rendered; such retroactive changes are not applied to already-committed output. Healing is a whole-document transform, so it is used only for `preview`/`finish`, not for committed output.

The `md4x` CLI exposes this via `--format=ansi --stream` (see [CLI docs](../AGENTS.md)).

## Shared JSON Writer (`md4x-json.h`)

Header-only utility providing JSON serialization and YAML-to-JSON conversion helpers. Used by both the AST and meta renderers.

```c
#include "md4x-json.h"
```

**Key components:**

- `JSON_WRITER` — Streaming JSON writer struct with callback-based output
- `json_write()` / `json_write_str()` — Raw and string output helpers
- `json_write_escaped()` / `json_write_string()` — JSON-escaped string output
- `json_write_yaml_props()` — Parses YAML frontmatter and writes key-value pairs as JSON properties (using libfyaml)

## Meta Renderer API (`md4x-meta.h`)

Lightweight metadata extractor that parses frontmatter and headings from Markdown:

```c
int md_meta(const MD_CHAR* input, MD_SIZE input_size,
            void (*process_output)(const MD_CHAR*, MD_SIZE, void*),
            void* userdata, unsigned parser_flags, unsigned renderer_flags);
```

Produces a flat JSON object with frontmatter properties spread at the top level plus a `headings` array. No AST construction — uses SAX callbacks to capture only frontmatter text and heading plain text.

**Example output:**

```json
{
  "title": "Hello",
  "tags": ["a", "b"],
  "headings": [
    { "level": 1, "text": "My Doc" },
    { "level": 2, "text": "Section 1" }
  ]
}
```

### Renderer Flags (`MD_META_FLAG_*`)

| Flag                         | Value    | Description                                   |
| ---------------------------- | -------- | --------------------------------------------- |
| `MD_META_FLAG_DEBUG`         | `0x0001` | Send debug output from `md_parse()` to stderr |
| `MD_META_FLAG_SKIP_UTF8_BOM` | `0x0002` | Skip UTF-8 BOM at input start                 |

### Rendering Details

- Frontmatter YAML properties are spread as top-level JSON keys (using libfyaml for full YAML 1.1 support)
- Headings are collected as `{"level": N, "text": "..."}` objects in the `headings` array
- Heading text is extracted as plain text — inline formatting (bold, italic, code, etc.) is stripped
- HTML entities in headings are resolved to UTF-8 characters
- Uses streaming renderer pattern (like HTML renderer), no AST construction

## Text Renderer API (`md4x-text.h`)

Strips markdown formatting and produces plain text output:

```c
int md_text(const MD_CHAR* input, MD_SIZE input_size,
            void (*process_output)(const MD_CHAR*, MD_SIZE, void*),
            void* userdata, unsigned parser_flags, unsigned renderer_flags);
```

### Renderer Flags (`MD_TEXT_FLAG_*`)

| Flag                         | Value    | Description                                   |
| ---------------------------- | -------- | --------------------------------------------- |
| `MD_TEXT_FLAG_DEBUG`         | `0x0001` | Send debug output from `md_parse()` to stderr |
| `MD_TEXT_FLAG_SKIP_UTF8_BOM` | `0x0002` | Skip UTF-8 BOM at input start                 |

### Rendering Details

- All inline formatting (bold, italic, underline, strikethrough, code spans) stripped — only text content remains
- Headings: plain text + newline
- Paragraphs: plain text + newline
- Lists: `- ` (unordered) or `1. ` (ordered) prefix with 2-space nesting indentation
- Task lists: `[x] ` / `[ ] ` prefix
- Code blocks: verbatim with 2-space indent
- Blockquotes: `> ` prefix (nested)
- Horizontal rules: `---`
- Tables: tab-separated cells
- Links: text content only (URL not shown)
- Images: alt text only
- Frontmatter: stripped (no output)
- Components/templates: transparent (children rendered normally)
- Alerts: type label + content with `> ` prefix
- Entities resolved to UTF-8 characters
- Raw HTML: stripped (no output)
- Uses streaming renderer pattern (like HTML renderer), no AST construction

## Heal Utility API (`md4x-heal.h`)

Fixes incomplete/streaming Markdown text so it renders correctly mid-stream. This is a **pre-parser text transform** — it does not use `md_parse()` and has no parser dependency.

Inspired by [remend](https://github.com/vercel/streamdown/tree/main/packages/remend).

```c
int md_heal(const char* input, unsigned input_size,
            void (*process_output)(const char*, unsigned, void*),
            void* userdata);
```

Returns 0 on success, -1 on error.

### Healing Operations (applied in priority order)

1. **Comparison operators** — Escapes `>` as `\>` in list items where it's a comparison operator (e.g., `- > 5` → `- \> 5`)
2. **HTML tags** — Strips incomplete HTML tags at end of text (e.g., `text <div` → `text`)
3. **Setext headings** — Appends zero-width space to 1-2 char `-`/`=` lines to prevent misinterpretation as heading underlines
4. **Links/images** — Completes incomplete link URLs with `()`, removes incomplete link brackets, removes incomplete image markup entirely
5. **Bold-italic (`\***`)** — Closes unclosed `\*\*\*` markers
6. **Bold (`**`)** — Closes unclosed `**`markers, handles half-complete`**text\*`→`**text**`
7. **Italic (`__`)** — Closes unclosed `__` markers, handles half-complete `__text_` → `__text__`
8. **Italic (`*`)** — Closes unclosed single `*` markers
9. **Italic (`_`)** — Closes unclosed single `_` markers
10. **Inline code** — Closes unclosed backticks
11. **Strikethrough (`~~`)** — Closes unclosed `~~` markers, handles half-complete `~~text~` → `~~text~~`
12. **KaTeX (`$$`)** — Closes unclosed `$$` math blocks (preserves newlines for block math)
13. **Code blocks** — Closes unclosed fenced code blocks (` ``` `)

### Context Awareness

- Formatting inside fenced code blocks is never healed
- Complete inline code spans are respected (no emphasis healing inside them)
- Math blocks (`$`/`$$`) are tracked to avoid false emphasis healing
- Link/image URLs are tracked to avoid false underscore healing
- HTML tag context is tracked
- Trailing single spaces are stripped (double spaces preserved for line breaks)
