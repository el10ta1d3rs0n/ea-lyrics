/* ea-lyrics — synced + romanized lyrics TUI, companion for ea-music.
 * 
 * Config: ~/.config/ea-lyrics/ea-lyrics.conf
 * Build:  make
 * Run:    ea-lyrics [-s /path/to/mpv.sock] [--gen-config]
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <curl/curl.h>
#include <glob.h>
#include <locale.h>
#include <math.h>
#include <ncurses.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <unicode/ustring.h>
#include <unicode/utrans.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ───────────── configuration ───────────── */
typedef struct {
    int romanize_default;
    int romanized_primary;
    int hide_secondary;
    int line_spacing;
    int primary_bold;
    int secondary_bold;
    int secondary_dim;
    char cache_dir[256];
    char providers[128];
    char key_romanize;
    char key_search;
    char key_quit;
    char key_pause;
    char key_offset_up;
    char key_offset_down;
    char key_toggle_hide;
    char key_provider_menu;
} Config;

static Config cfg = {
    .romanize_default = 1,
    .romanized_primary = 1,
    .hide_secondary = 1,
    .line_spacing = 0,
    .primary_bold = 1,
    .secondary_bold = 0,
    .secondary_dim = 1,
    .cache_dir = "/tmp/cache/ea-lyrics",
    .providers = "lrclib,netease,musixmatch,genius,kugou",
    .key_romanize = 'r',
    .key_search = 's',
    .key_quit = 'q',
    .key_pause = ' ',
    .key_offset_up = ']',
    .key_offset_down = '[',
    .key_toggle_hide = 'h',
    .key_provider_menu = 'p'
};

static char *get_config_path(char *path, size_t n) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) snprintf(path, n, "%s/ea-lyrics/ea-lyrics.conf", xdg);
    else snprintf(path, n, "%s/.config/ea-lyrics/ea-lyrics.conf", home ? home : ".");
    
    char dir[512];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; mkdir(dir, 0755); }
    return path;
}

static void save_config(void) {
    char path[512];
    get_config_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    
    fprintf(f, "# ea-lyrics configuration\n\n");
    fprintf(f, "romanize_default=%d\n", cfg.romanize_default);
    fprintf(f, "romanized_primary=%d\n", cfg.romanized_primary);
    fprintf(f, "hide_secondary=%d\n", cfg.hide_secondary);
    fprintf(f, "line_spacing=%d\n", cfg.line_spacing);
    fprintf(f, "primary_bold=%d\n", cfg.primary_bold);
    fprintf(f, "secondary_bold=%d\n", cfg.secondary_bold);
    fprintf(f, "secondary_dim=%d\n", cfg.secondary_dim);
    fprintf(f, "cache_dir=%s\n", cfg.cache_dir);
    fprintf(f, "# Comma-separated list of lyric providers in order of priority.\n");
    fprintf(f, "# Options: lrclib, netease, musixmatch, genius, kugou\n");
    fprintf(f, "providers=%s\n", cfg.providers);
    fprintf(f, "key_romanize=%c\n", cfg.key_romanize);
    fprintf(f, "key_search=%c\n", cfg.key_search);
    fprintf(f, "key_quit=%c\n", cfg.key_quit);
    if (cfg.key_pause == ' ') fprintf(f, "key_pause=space\n");
    else fprintf(f, "key_pause=%c\n", cfg.key_pause);
    fprintf(f, "key_offset_up=%c\n", cfg.key_offset_up);
    fprintf(f, "key_offset_down=%c\n", cfg.key_offset_down);
    fprintf(f, "key_toggle_hide=%c\n", cfg.key_toggle_hide);
    fprintf(f, "key_provider_menu=%c\n", cfg.key_provider_menu);
    fclose(f);
}

static void gen_config(void) {
    char path[512];
    get_config_path(path, sizeof path);
    save_config();
    printf("Generated config at: %s\n", path);
}

static void load_config(void) {
    char path[512];
    get_config_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p;
        char *val = eq + 1;
        
        char *ke = key + strlen(key);
        while (ke > key && isspace((unsigned char)*(ke-1))) *--ke = 0;
        
        while (isspace((unsigned char)*val)) val++;
        char *ve = val + strlen(val);
        while (ve > val && isspace((unsigned char)*(ve-1))) *--ve = 0;

        if (!strcmp(key, "romanize_default")) cfg.romanize_default = atoi(val);
        else if (!strcmp(key, "romanized_primary")) cfg.romanized_primary = atoi(val);
        else if (!strcmp(key, "hide_secondary")) cfg.hide_secondary = atoi(val);
        else if (!strcmp(key, "line_spacing")) {
            cfg.line_spacing = atoi(val);
            if (cfg.line_spacing < 0) cfg.line_spacing = 0;
            if (cfg.line_spacing > 3) cfg.line_spacing = 3;
        }
        else if (!strcmp(key, "primary_bold")) cfg.primary_bold = atoi(val);
        else if (!strcmp(key, "secondary_bold")) cfg.secondary_bold = atoi(val);
        else if (!strcmp(key, "secondary_dim")) cfg.secondary_dim = atoi(val);
        else if (!strcmp(key, "cache_dir")) snprintf(cfg.cache_dir, sizeof cfg.cache_dir, "%s", val);
        else if (!strcmp(key, "providers")) snprintf(cfg.providers, sizeof cfg.providers, "%s", val);
        else if (!strcmp(key, "key_romanize") && *val) cfg.key_romanize = *val;
        else if (!strcmp(key, "key_search") && *val) cfg.key_search = *val;
        else if (!strcmp(key, "key_quit") && *val) cfg.key_quit = *val;
        else if (!strcmp(key, "key_pause") && *val) {
            if (!strcmp(val, "space")) cfg.key_pause = ' ';
            else cfg.key_pause = *val;
        }
        else if (!strcmp(key, "key_offset_up") && *val) cfg.key_offset_up = *val;
        else if (!strcmp(key, "key_offset_down") && *val) cfg.key_offset_down = *val;
        else if (!strcmp(key, "key_toggle_hide") && *val) cfg.key_toggle_hide = *val;
        else if (!strcmp(key, "key_provider_menu") && *val) cfg.key_provider_menu = *val;
    }
    fclose(f);
}

/* ───────────── helpers ───────────── */
static const char *ci_strstr(const char *hay, const char *needle) {
    if (!needle || !*needle) return hay;
    for (; *hay; hay++) {
        const char *a = hay, *b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*b) return hay;
    }
    return NULL;
}

/* ───────────── minimal JSON scanner ───────────── */
static const char *skip_ws(const char *p) { while (*p && isspace((unsigned char)*p)) p++; return p; }

static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++) if (*p == '\\' && p[1]) p++;
        return *p ? p + 1 : p;
    }
    if (*p == '{' || *p == '[') {
        int d = 0;
        do {
            if (*p == '"') { p = skip_value(p); continue; }
            if (*p == '{' || *p == '[') d++;
            else if (*p == '}' || *p == ']') d--;
            p++;
        } while (*p && d > 0);
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p)) p++;
    return p;
}

static const char *json_find(const char *p, const char *key) {
    p = skip_ws(p);
    if (*p != '{') return NULL;
    p++;
    size_t kl = strlen(key);
    for (;;) {
        p = skip_ws(p);
        if (*p != '"') return NULL;
        const char *ks = p + 1, *ke = skip_value(p) - 1;
        p = skip_ws(ke + 1);
        if (*p != ':') return NULL;
        p = skip_ws(p + 1);
        if ((size_t)(ke - ks) == kl && !strncmp(ks, key, kl)) return p;
        p = skip_ws(skip_value(p));
        if (*p == ',') p++; else return NULL;
    }
}

static int put_utf8(char *o, unsigned c) {
    if (c < 0x80) { o[0] = c; return 1; }
    if (c < 0x800) { o[0] = 0xC0 | c >> 6; o[1] = 0x80 | (c & 63); return 2; }
    if (c < 0x10000) { o[0] = 0xE0 | c >> 12; o[1] = 0x80 | (c >> 6 & 63); o[2] = 0x80 | (c & 63); return 3; }
    o[0] = 0xF0 | c >> 18; o[1] = 0x80 | (c >> 12 & 63); o[2] = 0x80 | (c >> 6 & 63); o[3] = 0x80 | (c & 63); return 4;
}

static char *json_str(const char *p) {
    if (!p || *p != '"') return NULL;
    // Allocate exactly the maximum possible decoded size upfront (avoiding realloc)
    char *o = malloc((size_t)(skip_value(p) - p) + 4), *w = o;
    if (!o) return NULL;
    for (p++; *p && *p != '"';) {
        if (*p != '\\') { *w++ = *p++; continue; }
        p++;
        switch (*p) {
        case 'n': *w++ = '\n'; p++; break;
        case 't': *w++ = '\t'; p++; break;
        case 'r': *w++ = '\r'; p++; break;
        case 'b': *w++ = '\b'; p++; break;
        case 'f': *w++ = '\f'; p++; break;
        case 'u': {
            char h[5] = {0}; memcpy(h, p + 1, 4); unsigned c = strtoul(h, NULL, 16); p += 5;
            if (c >= 0xD800 && c < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
                memcpy(h, p + 2, 4); unsigned lo = strtoul(h, NULL, 16); p += 6;
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            }
            w += put_utf8(w, c); break;
        }
        case 0: break;
        default: *w++ = *p++;
        }
    }
    *w = 0;
    return o;
}

/* ───────────── romanization (ICU) ───────────── */
static UTransliterator *tr;
static pthread_mutex_t rom_mu = PTHREAD_MUTEX_INITIALIZER;

static void rom_init(void) {
    UErrorCode e = U_ZERO_ERROR;
    UChar id[96];
    int32_t idlen = 0;
    u_strFromUTF8(id, 96, &idlen, "Any-Latin; NFC; Latin-ASCII", -1, &e);
    if (U_FAILURE(e)) { tr = NULL; return; }
    tr = utrans_openU(id, idlen, UTRANS_FORWARD, NULL, 0, NULL, &e);
    if (U_FAILURE(e)) tr = NULL;
}

static char *romanize(const char *s) {
    if (!tr) return NULL;
    int ascii = 1;
    for (const char *p = s; *p; p++) if ((unsigned char)*p >= 0x80) { ascii = 0; break; }
    if (ascii) return NULL;

    size_t sl = strlen(s);
    UErrorCode e = U_ZERO_ERROR;
    int32_t n = 0, cap = (int32_t)sl * 16 + 64;
    UChar *b = malloc(sizeof(UChar) * (size_t)cap);
    if (!b) return NULL;
    u_strFromUTF8(b, cap, &n, s, -1, &e);
    int32_t len = n, lim = n;
    if (U_SUCCESS(e)) {
        pthread_mutex_lock(&rom_mu);
        utrans_transUChars(tr, b, &len, cap, 0, &lim, &e);
        pthread_mutex_unlock(&rom_mu);
    }
    char *out = NULL;
    if (U_SUCCESS(e)) {
        out = malloc((size_t)len * 4 + 1);
        if (out) {
            int32_t ol = 0; UErrorCode e2 = U_ZERO_ERROR;
            u_strToUTF8(out, len * 4 + 1, &ol, b, len, &e2);
            out[U_SUCCESS(e2) ? ol : 0] = 0;
            if (!*out || !strcmp(out, s)) { free(out); out = NULL; }
        }
    }
    free(b);
    return out;
}

/* ───────────── state ───────────── */
typedef struct { double t; char *text, *rom; } Line;
static Line *L; static int nL;
static char status[1024] = "waiting for mpv…";
static char current_provider[32] = "";
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int gen;

static void free_arr(Line *a, int n) { 
    for (int i = 0; i < n; i++) { free(a[i].text); free(a[i].rom); } 
    free(a); 
}

static void set_status(int g, const char *fmt, ...) {
    va_list ap;
    pthread_mutex_lock(&mu);
    if (g == gen) { va_start(ap, fmt); vsnprintf(status, sizeof status, fmt, ap); va_end(ap); }
    pthread_mutex_unlock(&mu);
}

/* ───────────── LRC parsing ───────────── */
static int cmp_line(const void *a, const void *b) {
    double d = ((const Line *)a)->t - ((const Line *)b)->t;
    return d < 0 ? -1 : d > 0;
}

static Line *parse_lrc(const char *lrc, int *out_n) {
    char *copy = strdup(lrc), *save = NULL;
    Line *a = NULL; int n = 0, cap = 0;
    for (char *ln = strtok_r(copy, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
        double ts[16]; int nt = 0; char *p = ln;
        while (*p == '[' && nt < 16) {
            int m; double s;
            if (sscanf(p, "[%d:%lf]", &m, &s) != 2) break;
            ts[nt++] = m * 60 + s;
            p = strchr(p, ']'); if (!p) break; p++;
        }
        if (!nt || !p) continue;
        while (isspace((unsigned char)*p)) p++;
        size_t l = strlen(p); while (l && isspace((unsigned char)p[l - 1])) l--;
        for (int i = 0; i < nt; i++) {
            if (n == cap) { cap = cap ? cap * 2 : 128; a = realloc(a, cap * sizeof *a); }
            a[n].t = ts[i]; a[n].text = strndup(p, l); a[n].rom = NULL; n++;
        }
    }
    free(copy);
    if (n) qsort(a, n, sizeof *a, cmp_line);
    *out_n = n;
    return a;
}

/* ───────────── network + cache ───────────── */
struct buf { char *d; size_t n; size_t cap; };

static size_t wcb(void *p, size_t s, size_t m, void *u) {
    struct buf *b = u; size_t k = s * m;
    if (b->n + k + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 4096;
        while (ncap < b->n + k + 1) ncap *= 2;
        char *nd = realloc(b->d, ncap);
        if (!nd) return 0;
        b->d = nd; b->cap = ncap;
    }
    memcpy(b->d + b->n, p, k); 
    b->n += k; 
    b->d[b->n] = 0;
    return k;
}

static char *http_get(const char *url) {
    CURL *c = curl_easy_init(); 
    struct buf b = {0};
    if (!c) return NULL;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wcb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "ea-lyrics/0.2 (https://github.com)");
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) { free(b.d); return NULL; }
    return b.d;
}

static void cache_file(const char *q, char *out, size_t n) {
    char dir[256];
    if (cfg.cache_dir[0]) snprintf(dir, sizeof dir, "%s", cfg.cache_dir);
    else snprintf(dir, sizeof dir, "/tmp/cache/ea-lyrics");
    
    for (char *p = dir + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(dir, 0777); *p = '/'; }
    }
    mkdir(dir, 0777);
    
    uint64_t hv = 1469598103934665603ULL;
    for (const char *p = q; *p; p++) { hv ^= (unsigned char)tolower((unsigned char)*p); hv *= 1099511628211ULL; }
    
    snprintf(out, n, "%.200s/%016llx.json", dir, (unsigned long long)hv);
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *s = malloc((size_t)n + 1); size_t r = fread(s, 1, (size_t)n, f); s[r] = 0; fclose(f);
    if (!r) { free(s); return NULL; }
    return s;
}

/* ───────────── provider implementations ───────────── */
typedef struct {
    char *lrc;
    char *rom;
    char *title;
    char *artist;
} LyricsResult;

static void free_result(LyricsResult *r) {
    free(r->lrc); free(r->rom); free(r->title); free(r->artist);
    r->lrc = r->rom = r->title = r->artist = NULL;
}

/* 1. LRCLIB */
static void lrclib_fetch(const char *query, double dur, LyricsResult *out) {
    char cf[512];
    cache_file(query, cf, sizeof cf);
    char *obj = read_file(cf);
    if (!obj) {
        CURL *c = curl_easy_init(); 
        char *esc = c ? curl_easy_escape(c, query, 0) : NULL;
        char url[1200]; snprintf(url, sizeof url, "https://lrclib.net/api/search?q=%s", esc ? esc : "");
        if (esc) curl_free(esc); 
        if (c) curl_easy_cleanup(c);
        
        char *resp = http_get(url);
        if (resp) {
            const char *p = skip_ws(resp), *best = NULL, *best_end = NULL; double bd = 1e18;
            if (*p == '[') {
                for (p = skip_ws(p + 1); *p && *p != ']';) {
                    const char *end = skip_value(p), *sl = json_find(p, "syncedLyrics");
                    if (sl && *sl == '"' && sl[1] != '"') {
                        const char *dv = json_find(p, "duration");
                        double d = dur > 0 && dv ? fabs(strtod(dv, NULL) - dur) : 0;
                        if (!best || d < bd) { best = p; best_end = end; bd = d; }
                    }
                    p = skip_ws(end); if (*p == ',') p = skip_ws(p + 1);
                }
            }
            if (best) obj = strndup(best, (size_t)(best_end - best));
            free(resp);
            if (obj) { FILE *f = fopen(cf, "wb"); if (f) { fputs(obj, f); fclose(f); } }
        }
    }
    if (!obj) return;
    out->lrc = json_str(json_find(obj, "syncedLyrics"));
    out->title = json_str(json_find(obj, "trackName"));
    out->artist = json_str(json_find(obj, "artistName"));
    free(obj);
}

/* 2. NetEase */
static void netease_fetch(const char *query, LyricsResult *out) {
    CURL *c = curl_easy_init();
    char *esc = c ? curl_easy_escape(c, query, 0) : NULL;
    char url[1024];
    snprintf(url, sizeof url, "https://music.163.com/api/cloudsearch/pc?s=%s&type=1&limit=1", esc ? esc : "");
    if (esc) curl_free(esc); 
    if (c) curl_easy_cleanup(c);
    
    char *resp = http_get(url);
    if (!resp) return;
    const char *res_obj = json_find(resp, "result");
    long long id = 0;
    if (res_obj) {
        const char *songs_arr = json_find(res_obj, "songs");
        if (songs_arr) {
            const char *first_song = skip_ws(songs_arr + 1);
            if (*first_song == '{') {
                const char *id_val = json_find(first_song, "id");
                if (id_val) id = strtoll(id_val, NULL, 10);
                const char *name_val = json_find(first_song, "name");
                if (name_val) out->title = json_str(name_val);
                const char *ar_arr = json_find(first_song, "ar");
                if (ar_arr) {
                    const char *ar_first = skip_ws(ar_arr + 1);
                    if (*ar_first == '{') {
                        const char *artist_name = json_find(ar_first, "name");
                        if (artist_name) out->artist = json_str(artist_name);
                    }
                }
            }
        }
    }
    free(resp);
    if (id <= 0) return;

    snprintf(url, sizeof url, "https://interface3.music.163.com/api/song/lyric?id=%lld&lv=1&kv=1&tv=-1", id);
    resp = http_get(url);
    if (!resp) return;

    const char *lrc_obj = json_find(resp, "lrc");
    if (lrc_obj) {
        const char *lyric_str = json_find(lrc_obj, "lyric");
        if (lyric_str) out->lrc = json_str(lyric_str);
    }
    const char *rom_obj = json_find(resp, "romalrc");
    if (rom_obj) {
        const char *rom_str = json_find(rom_obj, "lyric");
        if (rom_str) out->rom = json_str(rom_str);
    }
    const char *t_obj = json_find(resp, "tlyric");
    if (t_obj && !out->rom) {
        const char *t_str = json_find(t_obj, "lyric");
        if (t_str) out->rom = json_str(t_str);
    }
    free(resp);
}

/* 3. Musixmatch (desktop API anonymous) */
static void musixmatch_fetch(const char *query, LyricsResult *out) {
    CURL *c = curl_easy_init();
    char *esc = c ? curl_easy_escape(c, query, 0) : NULL;
    char url[1024];
    snprintf(url, sizeof url, "https://apic-desktop.musixmatch.com/ws/1.1/macro.subtitles.get?format=json&q_track=%s&namespace=lyrics_synched&f_subtitle_length_max_deviation=1", esc ? esc : "");
    if (esc) curl_free(esc); 
    if (c) curl_easy_cleanup(c);
    
    char *resp = http_get(url);
    if (!resp) return;

    const char *macro_obj = json_find(resp, "macro_calls");
    if (macro_obj) {
        const char *sub_obj = json_find(macro_obj, "track.subtitles.get");
        if (sub_obj) {
            const char *msg_obj = json_find(sub_obj, "message");
            if (msg_obj) {
                const char *body_obj = json_find(msg_obj, "body");
                if (body_obj) {
                    const char *sl_obj = json_find(body_obj, "subtitle_list");
                    if (sl_obj) {
                        const char *first_sl = skip_ws(sl_obj + 1);
                        if (*first_sl == '{') {
                            const char *subtitle_obj = json_find(first_sl, "subtitle");
                            if (subtitle_obj) {
                                const char *st = json_find(subtitle_obj, "subtitle_body");
                                if (st) out->lrc = json_str(st);
                            }
                        }
                    }
                }
            }
        }
    }
    free(resp);
}

/* 4. Genius */
static void genius_fetch(const char *query, LyricsResult *out) {
    CURL *c = curl_easy_init();
    char *esc = c ? curl_easy_escape(c, query, 0) : NULL;
    char url[1024];
    snprintf(url, sizeof url, "https://geniurl.com/api/search?q=%s", esc ? esc : "");
    if (esc) curl_free(esc); 
    if (c) curl_easy_cleanup(c);
    
    char *resp = http_get(url);
    if (!resp) return;

    const char *result_arr = json_find(resp, "result");
    if (result_arr && *result_arr == '[') {
        const char *first = skip_ws(result_arr + 1);
        if (*first == '{') {
            const char *title = json_find(first, "title");
            if (title) out->title = json_str(title);
            const char *artist = json_find(first, "primary_artist");
            if (artist) out->artist = json_str(artist);
        }
    }
    free(resp);
}

/* 5. Kugou (via LrcApi) */
static void kugou_fetch(const char *query, LyricsResult *out) {
    CURL *c = curl_easy_init();
    char *esc = c ? curl_easy_escape(c, query, 0) : NULL;
    char url[1024];
    snprintf(url, sizeof url, "https://api.lrc.cx/lyrics?title=%s", esc ? esc : "");
    if (esc) curl_free(esc); 
    if (c) curl_easy_cleanup(c);
    
    char *resp = http_get(url);
    if (!resp) return;

    if (resp[0] == '{') {
        const char *lyrics_val = json_find(resp, "lyrics");
        if (lyrics_val) out->lrc = json_str(lyrics_val);
    } else {
        out->lrc = strdup(resp);
    }
    free(resp);
}

/* ───────────── loader ───────────── */
typedef struct { char q[512]; double dur; int gen; } Job;

static void *loader(void *arg) {
    Job *j = arg;
    LyricsResult res = {0};
    int found = 0;

    pthread_mutex_lock(&mu);
    current_provider[0] = 0;
    pthread_mutex_unlock(&mu);

    char providers_copy[128];
    snprintf(providers_copy, sizeof providers_copy, "%s", cfg.providers);
    
    char *provider = strtok(providers_copy, ",");
    while (provider && !found) {
        while (isspace((unsigned char)*provider)) provider++;
        
        if (strcmp(provider, "lrclib") == 0) {
            set_status(j->gen, "searching LRCLIB: %s", j->q);
            lrclib_fetch(j->q, j->dur, &res);
        } 
        else if (strcmp(provider, "netease") == 0) {
            set_status(j->gen, "searching NetEase: %s", j->q);
            netease_fetch(j->q, &res);
        }
        else if (strcmp(provider, "musixmatch") == 0) {
            set_status(j->gen, "searching Musixmatch: %s", j->q);
            musixmatch_fetch(j->q, &res);
        }
        else if (strcmp(provider, "genius") == 0) {
            set_status(j->gen, "searching Genius: %s", j->q);
            genius_fetch(j->q, &res);
        }
        else if (strcmp(provider, "kugou") == 0) {
            set_status(j->gen, "searching Kugou: %s", j->q);
            kugou_fetch(j->q, &res);
        }
        
        if (res.lrc && *res.lrc) {
            found = 1;
            pthread_mutex_lock(&mu);
            if (j->gen == gen) snprintf(current_provider, sizeof current_provider, "%s", provider);
            pthread_mutex_unlock(&mu);
        }
        else free_result(&res);
        
        provider = strtok(NULL, ",");
    }

    if (!found) {
        pthread_mutex_lock(&mu);
        if (j->gen == gen) { 
            free_arr(L, nL); L = NULL; nL = 0; 
            snprintf(status, sizeof status, "no lyrics found: %.200s", j->q); 
        }
        pthread_mutex_unlock(&mu);
        free(j); 
        return NULL;
    }

    int n = 0; 
    Line *a = parse_lrc(res.lrc, &n);

    if (res.rom) {
        int rom_n = 0;
        Line *rom_a = parse_lrc(res.rom, &rom_n);
        for (int i = 0; i < n && i < rom_n; i++) {
            // Memory optimization: Transfer ownership instead of strdup
            a[i].rom = rom_a[i].text; 
            rom_a[i].text = NULL; 
        }
        free_arr(rom_a, rom_n);
    }

    for (int i = 0; i < n; i++) {
        if (a[i].text[0] && !a[i].rom) {
            a[i].rom = romanize(a[i].text);
        }
    }

    pthread_mutex_lock(&mu);
    if (j->gen == gen) {
        free_arr(L, nL); L = a; nL = n; a = NULL;
        if (res.title && res.artist) snprintf(status, sizeof status, "%s — %s", res.title, res.artist);
        else if (res.title) snprintf(status, sizeof status, "%s", res.title);
        else snprintf(status, sizeof status, "Lyrics loaded");
    }
    pthread_mutex_unlock(&mu);

    if (a) free_arr(a, n);
    free_result(&res);
    free(j);
    return NULL;
}

static void start_job(const char *q, double dur) {
    Job *j = calloc(1, sizeof *j);
    if (!j) return;
    snprintf(j->q, sizeof j->q, "%s", q); j->dur = dur;
    pthread_mutex_lock(&mu); j->gen = ++gen; pthread_mutex_unlock(&mu);
    pthread_t th;
    if (pthread_create(&th, NULL, loader, j)) { free(j); return; }
    pthread_detach(th);
}

/* ───────────── mpv IPC ───────────── */
static int mfd = -1, mid; static char mpath[512], mbuf[65536]; static size_t mlen;

static int is_socket_connectable(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%.*s", (int)(sizeof(a.sun_path)-1), path);
    int ok = (connect(fd, (struct sockaddr *)&a, sizeof a) == 0);
    close(fd);
    return ok;
}

typedef struct { char path[256]; long long mtime; long long pid; } SockInfo;

static void find_best_socket(char *out, size_t out_len) {
    const char *es = getenv("EA_MPV_SOCKET");
    if (es && *es) { snprintf(out, out_len, "%s", es); return; }

    glob_t g;
    if (glob("/tmp/ytdlp_mpv_*.sock", 0, NULL, &g) == 0) {
        SockInfo *socks = malloc(g.gl_pathc * sizeof(SockInfo));
        int n = 0;
        for (size_t i = 0; i < g.gl_pathc; i++) {
            struct stat st;
            if (stat(g.gl_pathv[i], &st) == 0) {
                long long pid = 0;
                sscanf(g.gl_pathv[i], "/tmp/ytdlp_mpv_%lld.sock", &pid);
                snprintf(socks[n].path, sizeof socks[n].path, "%s", g.gl_pathv[i]);
                socks[n].mtime = (long long)st.st_mtime;
                socks[n].pid = pid;
                n++;
            }
        }
        globfree(&g);
        
        for (int i = 0; i < n - 1; i++) {
            for (int j = i + 1; j < n; j++) {
                if (socks[j].mtime > socks[i].mtime || (socks[j].mtime == socks[i].mtime && socks[j].pid > socks[i].pid)) {
                    SockInfo tmp = socks[i]; socks[i] = socks[j]; socks[j] = tmp;
                }
            }
        }
        
        for (int i = 0; i < n; i++) {
            if (is_socket_connectable(socks[i].path)) {
                snprintf(out, out_len, "%s", socks[i].path);
                free(socks);
                return;
            }
        }
        free(socks);
    }

    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt || !*rt) rt = "/tmp";
    snprintf(out, out_len, "%s/ea-music-mpv.sock", rt);
}

static void mpv_close(void) { if (mfd >= 0) close(mfd); mfd = -1; mlen = 0; }

static int mpv_open(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0); if (fd < 0) return -1;
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%.*s", (int)(sizeof(a.sun_path)-1), mpath);
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    a.sun_len = sizeof a;
#endif
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    mfd = fd; mlen = 0; return 0;
}

static char *mpv_get_raw(const char *prop) {
    if (mfd < 0 && mpv_open() < 0) return NULL;
    char req[256]; int id = ++mid;
    int n = snprintf(req, sizeof req, "{\"command\":[\"get_property\",\"%s\"],\"request_id\":%d}\n", prop, id);
    if (send(mfd, req, (size_t)n, MSG_NOSIGNAL) != n) { mpv_close(); return NULL; }
    for (int tries = 0; tries < 4; tries++) {
        char *nl;
        while ((nl = memchr(mbuf, '\n', mlen))) {
            *nl = 0; char *res = NULL; int match = 0;
            const char *ri = json_find(mbuf, "request_id");
            if (ri && (int)strtol(ri, NULL, 10) == id) {
                match = 1;
                const char *er = json_find(mbuf, "error"), *d = json_find(mbuf, "data");
                if (er && !strncmp(er, "\"success\"", 9) && d) res = strndup(d, (size_t)(skip_value(d) - d));
            }
            size_t used = (size_t)(nl - mbuf) + 1; memmove(mbuf, mbuf + used, mlen - used); mlen -= used;
            if (match) return res;
        }
        if (mlen >= sizeof mbuf - 1) mlen = 0;
        struct pollfd pf = {mfd, POLLIN, 0};
        if (poll(&pf, 1, 50) <= 0) continue;
        ssize_t r = recv(mfd, mbuf + mlen, sizeof mbuf - 1 - mlen, 0);
        if (r <= 0) { mpv_close(); return NULL; }
        mlen += (size_t)r;
    }
    return NULL;
}

static int mpv_num(const char *prop, double *out) {
    char *r = mpv_get_raw(prop); if (!r) return 0;
    char *e; double v = strtod(r, &e); int ok = e != r; free(r);
    if (ok) *out = v;
    return ok;
}

static void mpv_cmd(const char *arr) {
    if (mfd < 0) return;
    char req[128]; int n = snprintf(req, sizeof req, "{\"command\":%s}\n", arr);
    if (send(mfd, req, (size_t)n, MSG_NOSIGNAL) != n) mpv_close();
}

/* ───────────── title cleanup ───────────── */
static void clean_title(const char *in, char *out, size_t n) {
    static const char *kw[] = {"official", "video", "audio", "lyric", "remaster", "visualizer", "4k", "hd", "mv", NULL};
    size_t o = 0;
    for (const char *p = in; *p && o < n - 1;) {
        if (*p == '(' || *p == '[') {
            const char *e = strchr(p, *p == '(' ? ')' : ']');
            if (e) {
                char tmp[256]; size_t k = (size_t)(e - p - 1); if (k >= sizeof tmp) k = sizeof tmp - 1;
                memcpy(tmp, p + 1, k); tmp[k] = 0;
                int bad = 0; for (int i = 0; kw[i]; i++) if (ci_strstr(tmp, kw[i])) bad = 1;
                if (bad) { p = e + 1; continue; }
            }
        }
        out[o++] = *p++;
    }
    out[o] = 0;
    char *t = (char *)ci_strstr(out, " - topic"); if (t) *t = 0;
    for (o = strlen(out); o && (out[o - 1] == ' ' || out[o - 1] == '-' || out[o - 1] == '|'); o--) out[o - 1] = 0;
}

/* ───────────── drawing ───────────── */
#define MAXR 8
static int wrap(const char *s, int width, const char **st, int *ln, int *cw) {
    int n = 0;
    if (width < 1) width = 1;
    while (*s && n < MAXR) {
        const char *p = s, *brk = NULL, *next; int col = 0, brkcol = 0, len, cols;
        while (*p) {
            wchar_t wc; int l = mbtowc(&wc, p, MB_CUR_MAX);
            if (l <= 0) { l = 1; wc = L'?'; }
            int w = wcwidth(wc); if (w < 0) w = 1;
            if (col + w > width) break;
            if (wc == L' ') { brk = p; brkcol = col; }
            col += w; p += l;
        }
        if (*p && brk && brk > s) { len = (int)(brk - s); cols = brkcol; next = brk + 1; }
        else { len = (int)(p - s); cols = col; next = p; }
        if (next == s) { next = s + 1; len = 1; cols = 1; }
        st[n] = s; ln[n] = len; cw[n] = cols; n++; s = next;
    }
    return n;
}

static int block(int i, int y, int w, int h, int cur, int rom, int draw) {
    const char *st[MAXR]; int ln[MAXR], cw[MAXR], rows = 0;
    const char *primary_txt = NULL;
    const char *secondary_txt = NULL;

    if (rom && cfg.romanized_primary && L[i].rom) {
        primary_txt = L[i].rom;
        secondary_txt = L[i].text;
    } else {
        primary_txt = L[i].text[0] ? L[i].text : "♪";
        secondary_txt = (rom && L[i].rom) ? L[i].rom : NULL;
    }

    if (cfg.hide_secondary) {
        secondary_txt = NULL; 
    }

    int n = wrap(primary_txt, w - 4, st, ln, cw);
    for (int k = 0; k < n; k++, rows++) {
        if (draw && y + rows >= 1 && y + rows < h - 4) {
            if (cur) {
                attron(A_BOLD | COLOR_PAIR(3));
                int start_x = (w - cw[k]) / 2;
                if (start_x >= 3) mvaddnstr(y + rows, start_x - 3, ">> ", 3);
                mvaddnstr(y + rows, start_x, st[k], ln[k]);
                int end_x = start_x + cw[k];
                if (end_x + 3 < w) mvaddnstr(y + rows, end_x, " <<", 3);
                attroff(A_BOLD | COLOR_PAIR(3));
            } else {
                attron(A_DIM | COLOR_PAIR(2));
                mvaddnstr(y + rows, (w - cw[k]) / 2, st[k], ln[k]);
                attroff(A_DIM | COLOR_PAIR(2));
            }
        }
    }

    if (secondary_txt) {
        n = wrap(secondary_txt, w - 2, st, ln, cw);
        for (int k = 0; k < n; k++, rows++) {
            if (draw && y + rows >= 1 && y + rows < h - 4) {
                int attr = cur ? COLOR_PAIR(1) : (A_DIM | COLOR_PAIR(1));
                attron(attr);
                mvaddnstr(y + rows, (w - cw[k]) / 2, st[k], ln[k]);
                attroff(attr);
            }
        }
    }
    
    if (cur) rows += 1;
    rows += cfg.line_spacing;
    return rows;
}

static void draw(double t, double off, int rom, double dur) {
    int h, w; getmaxyx(stdscr, h, w);
    erase();
    int sw = w > 2 ? w - 2 : 0;
    pthread_mutex_lock(&mu);

    attron(A_BOLD | COLOR_PAIR(3));
    if (current_provider[0])
        mvprintw(0, 1, "[%s] %.*s", current_provider, sw, status);
    else
        mvaddnstr(0, 1, status, sw);
    attroff(A_BOLD | COLOR_PAIR(3));

    int idx = -1;
    for (int lo = 0, hi = nL - 1; lo <= hi;) {
        int m = (lo + hi) / 2;
        if (L[m].t <= t + off) { idx = m; lo = m + 1; } else hi = m - 1;
    }
    
    int mid_y = h / 2, y = mid_y;
    for (int i = idx < 0 ? 0 : idx; i < nL && y < h - 4; i++) y += block(i, y, w, h, i == idx, rom, 1) + 1;
    y = mid_y - 1;
    for (int i = idx - 1; i >= 0 && y >= 2; i--) { y -= block(i, 0, w, h, 0, rom, 0); block(i, y, w, h, 0, rom, 1); y -= 1; }
    pthread_mutex_unlock(&mu);

    if (dur > 0 && w > 4) {
        int pw = w - 2;
        double pct = t / dur;
        if (pct < 0) pct = 0;
        if (pct > 1) pct = 1;
        int filled = (int)(pct * pw);
        attron(A_DIM);
        for (int i = 0; i < pw; i++) {
            if (i < filled) mvaddch(h - 3, i + 1, '=' | COLOR_PAIR(1));
            else if (i == filled) mvaddch(h - 3, i + 1, '>' | COLOR_PAIR(3));
            else mvaddch(h - 3, i + 1, '-' | COLOR_PAIR(2));
        }
        attroff(A_DIM);
    }

    char foot[200];
    snprintf(foot, sizeof foot,
             "%c romanize:%s  %c hide:%s  %c providers  %c search  %c %c offset %+.1fs  %c pause  <- -> seek  %c quit",
             cfg.key_romanize, rom ? "on" : "off", 
             cfg.key_toggle_hide, cfg.hide_secondary ? "on" : "off",
             cfg.key_provider_menu, cfg.key_search, 
             cfg.key_offset_down, cfg.key_offset_up, off, 
             cfg.key_pause, cfg.key_quit);
    attron(A_DIM);
    mvaddnstr(h - 1, 1, foot, sw);
    attroff(A_DIM);
    refresh();
}

/* ───────────── main ───────────── */
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    setlocale(LC_ALL, "");
    if (MB_CUR_MAX == 1) {
        if (!setlocale(LC_CTYPE, "C.UTF-8")) setlocale(LC_CTYPE, "en_US.UTF-8");
    }

    int custom_sock = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            snprintf(mpath, sizeof mpath, "%s", argv[++i]);
            custom_sock = 1;
        } else if (!strcmp(argv[i], "--gen-config")) {
            gen_config();
            return 0;
        } else {
            fprintf(stderr, "usage: %s [-s mpv-ipc-socket] [--gen-config]\n", argv[0]);
            return 1;
        }
    }

    load_config();
    rom_init();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0); timeout(50);
    if (has_colors() && start_color() != ERR) {
        if (use_default_colors() == ERR) {
            init_pair(1, COLOR_GREEN, COLOR_BLACK);
            init_pair(2, COLOR_WHITE, COLOR_BLACK);
            init_pair(3, COLOR_CYAN,  COLOR_BLACK);
        } else {
            init_pair(1, COLOR_GREEN, -1);
            init_pair(2, COLOR_WHITE, -1);
            init_pair(3, COLOR_CYAN,  -1);
        }
    }

    double t = 0, off = 0, dur = 0; int rom = cfg.romanize_default; char last[512] = "";
    time_t last_poll = 0;
    
    for (;;) {
        if (!custom_sock && mfd < 0) {
            char new_path[512];
            find_best_socket(new_path, sizeof new_path);
            if (strcmp(new_path, mpath) != 0) {
                snprintf(mpath, sizeof mpath, "%s", new_path);
            }
        }

        time_t now = time(NULL);
        if (now != last_poll) {
            last_poll = now;
            static const char *tkeys[] = {"metadata/by-key/title", "metadata/by-key/Title", "media-title", NULL};
            char *title = NULL;
            for (int ti = 0; tkeys[ti]; ti++) {
                char *r = mpv_get_raw(tkeys[ti]);
                title = r ? json_str(r) : NULL;
                free(r);
                if (title && *title) break;
                free(title); title = NULL;
            }

            if (!title && mfd < 0) {
                set_status(gen, "waiting for mpv socket: %s", mpath);
                last[0] = 0;
            } else if (title && *title && strncmp(title, "http", 4) && strcmp(title, last)) {
                snprintf(last, sizeof last, "%s", title);
                mpv_num("duration", &dur);
                char q[512]; clean_title(title, q, sizeof q);
                start_job(q, dur);
            }
            free(title);
        }
        
        mpv_num("time-pos", &t);
        draw(t, off, rom, dur);
        
        int k = getch();
        if (k == cfg.key_quit) break;
        else if (k == cfg.key_romanize) rom = !rom;
        else if (k == cfg.key_toggle_hide) {
            cfg.hide_secondary = !cfg.hide_secondary;
            save_config();
        }
        else if (k == cfg.key_offset_up) off += 0.5;
        else if (k == cfg.key_offset_down) off -= 0.5;
        else if (k == cfg.key_pause) mpv_cmd("[\"cycle\",\"pause\"]");
        else if (k == KEY_LEFT) mpv_cmd("[\"seek\",-5]");
        else if (k == KEY_RIGHT) mpv_cmd("[\"seek\",5]");
        else if (k == cfg.key_provider_menu) {
            char q[128] = ""; int h = getmaxy(stdscr);
            timeout(-1); echo(); curs_set(1);
            move(h - 1, 0); clrtoeol();
            printw(" providers [lrclib,netease,musixmatch,genius,kugou] > ");
            getnstr(q, sizeof q - 1);
            noecho(); curs_set(0); timeout(50);
            if (*q) {
                snprintf(cfg.providers, sizeof cfg.providers, "%s", q);
                save_config();
                if (*last) start_job(last, dur); 
            }
        }
        else if (k == cfg.key_search) {
            char q[256] = ""; int h = getmaxy(stdscr);
            timeout(-1); echo(); curs_set(1);
            move(h - 1, 0); clrtoeol(); printw(" search: "); getnstr(q, sizeof q - 1);
            noecho(); curs_set(0); timeout(50);
            if (*q) start_job(q, 0);
        }
    }

    endwin();
    curl_global_cleanup();
    return 0;
}
