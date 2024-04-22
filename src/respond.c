#include "respond.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include "array.h"
#include "httpcode.h"

enum {
    NBUF = 1024
};

static int fd;

static struct Buffer {
    size_t len, ptr;
    int look, isdead;
    char s[NBUF];
} buf;

static void BufReset(void) {
    buf.len = buf.ptr = 0;
    buf.look = -1;
    buf.isdead = 0;
}

static int LookRaw(void) {
    if (buf.isdead)
        return -1;
    if (buf.ptr < buf.len)
        return buf.s[buf.ptr];
    buf.len = recv(fd, buf.s, NBUF, 0);
    buf.ptr = 0;
    if (buf.len == 0) {
        buf.isdead = 1;
        return -1;
    }
    return buf.s[0];
}

static int PopRaw(void) {
    int c = LookRaw();
    if (c != -1)
        buf.ptr++;
    return c;
}

static int LookChar(void) {
    if (buf.look != -1)
        return buf.look;
    buf.look = PopRaw();
    if (buf.look != '\r')
        return buf.look;
    if (LookRaw() == '\n')
        buf.look = PopRaw();
    return buf.look;
}

static int PopChar(void) {
    int c = LookChar();
    buf.look = -1;
    return c;
}

static void SkipBlank(void) {
    int c;
    while ((c = LookChar()) != -1 && isblank(c))
        (void)PopChar();
}

static void PopWord(Str *s) {
    int c;
    SkipBlank();
    s->len = 0;
    while ((c = LookChar()) != -1 && !isspace(c))
        Append(s, PopChar());
    Append(s, '\0');
}

static int PopEOL(void) {
    if (LookChar() != '\n')
        return -1;
    (void)PopChar();
    return 0;
}

static void Send(const char *s, int n) {
    if (send(fd, s, n, 0) == -1)
        eprintf("send:");
}

static int StartsWith(const char *s, const char *prefix) {
    while (*prefix != '\0' && *s == *prefix) {
        s++;
        prefix++;
    }
    return *prefix == '\0';
}

static void EscapeHTML(Str *dst, const char *src) {
    dst->len = 0;
    for (; *src != '\0'; src++) {
        switch (*src) {
        case '&':
            Sprintf(dst, "&amp;");
            break;
        case '<':
            Sprintf(dst, "&lt;");
            break;
        case '>':
            Sprintf(dst, "&gt;");
            break;
        case '\"':
            Sprintf(dst, "&quot;");
            break;
        case '\'':
            Sprintf(dst, "&#39;");
            break;
        default:
            Sprintf(dst, "%c", *src);
        }
    }
}

static int HexValue(int c) {
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return 10 + (c - 'a');
    if ('A' <= c && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static int DecodeURL(Str *dst, const char *src) {
    int c1, c2;
    dst->len = 0;
    for (; *src != '\0'; src++) {
        switch (*src) {
        case '+':
            Sprintf(dst, " ");
            break;
        case '%':
            c1 = HexValue(src[1]);
            if (c1 == -1)
                return -1;
            c2 = HexValue(src[2]);
            if (c2 == -1)
                return -1;
            Sprintf(dst, "%c", 16*c1 + c2);
            src += 2;
            break;
        case '&':
        case '=':
            return -1;
        default:
            Sprintf(dst, "%c", *src);
        }
    }
    return 0;
}

static void EncodeURL(Str *dst, const char *src) {
    dst->len = 0;
    for (; *src != '\0'; src++) {
        if (isalnum(*src) || *src == '/' || *src == '.') {
            Sprintf(dst, "%c", *src);
            continue;
        }
        Sprintf(dst, "%%%2x", (unsigned char)*src);
    }
}

int httpcode;
Str status[3];
const char *contenttype;
Str content;
Str packet;
Str url;
typedef struct Header {
    Str name, value;
} Header;
#define HeaderArray Array(Header)
HeaderArray headers;

static void Done(void) {
    Str *s = &packet;
    s->len = 0;
    Sprintf(s, "HTTP/1.1 %d %s\r\n", httpcode, GetMessage(httpcode));
    Sprintf(s, "Server: pinch/0.1\r\n");
    Sprintf(s, "Connection: close\r\n");
    if (httpcode == TEMP_REDIR) {
        Str loc = {0};
        EncodeURL(&loc, content.arr);
        Sprintf(s, "Location: %s\r\n", loc.arr);
        ArrayRelease(&loc);
    } else {
        Sprintf(s, "Content-Length: %ld\r\n", StrLen(&content));
        if (StrLen(&content) > 0 && contenttype != NULL)
            Sprintf(s, "Content-Type: %s\r\n", contenttype);
        Sprintf(s, "\r\n");
        if (StrLen(&content) > 0 && strcmp(status[0].arr, "HEAD") != 0) {
            ArrayPinchN(s, StrLen(&content));
            memmove(&ArrayLast(s), content.arr, StrLen(&content)+1);
            s->len += StrLen(&content);
        }
    }
    Send(s->arr, StrLen(s));
}

static void SetError(int code, const char *err) {
    httpcode = code;
    contenttype = "text/html; charset=utf-8";
    const char *msg = GetMessage(code);
    Sprintf(&content, "<!DOCTYPE html><html lang=\"en\"><head>");
    Sprintf(&content, "<title>%d %s</title></head><body>",
            code, msg);
    Sprintf(&content, "<h1>%d %s</h1>", code, msg);
    Str errstr = {0};
    EscapeHTML(&errstr, err);
    Sprintf(&content, "<p>%s</p>", errstr.arr);
    ArrayRelease(&errstr);
    Sprintf(&content, "</body></html>");
}

static int PopHeader(void) {
    int c;
    if ((c = LookChar()) == -1 || isblank(c)) {
        SetError(BAD_REQ, "No header name");
        return -1;
    }
    if (c == ':' || c == '\n') {
        SetError(BAD_REQ, "Empty header name");
        return -1;
    }
    ArrayPinch(&headers);
    Header *h = &headers.arr[headers.len++];
    memset(h, 0, sizeof(*h));
    Append(&h->name, '\0');
    Append(&h->value, '\0');
    while ((c = LookChar()) != -1 && c != '\n' && c != ':') {
        Sprintf(&h->name, "%c", tolower(PopChar()));
    }
    if (c != ':') {
        SetError(BAD_REQ, "No header value");
        return -1;
    }
    (void)PopChar();
    do {
        SkipBlank();
        while ((c = LookChar()) != -1 && c != '\n') {
            Sprintf(&h->value, "%c", PopChar());
        }
        if (c != '\n') {
            SetError(BAD_REQ, "Header value not terminated");
            return -1;
        }
        (void)PopChar();
    } while ((c = LookChar()) != -1 && isblank(c));
    return 0;
}

static int PopHeaders(void) {
    int c;
    while ((c = LookChar()) != -1 && c != '\n') {
        if (PopHeader() == -1)
            return -1;
    }
    if (c == -1) {
        SetError(BAD_REQ, "Headers not terminated");
        return -1;
    }
    (void)PopChar();
    return 0;
}

static int WriteFile(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        weprintf("fopen:");
        switch (errno) {
        case EACCES:
            SetError(FORBIDDEN, "Access denied");
            return -1;
        case ENOENT:
            SetError(NOT_FOUND, "No such file");
            return -1;
        default:
            SetError(INTERNAL_ERR, "Something went wrong");
            return -1;
        }
        /* not reached */
    }
    httpcode = OK;
    int c;
    while ((c = fgetc(f)) != EOF)
        Append(&content, c);
    Append(&content, '\0');
    if (ferror(f)) {
        ArrayRelease(&content);
        SetError(INTERNAL_ERR, "Something went wrong (really bad)");
        return -1;
    }
    return 0;
}

typedef struct Entry {
    int type;
    char *name;
} Entry;

static int CompareFunction(const void *_s, const void *_t) {
    Entry *s = (Entry *)_s;
    Entry *t = (Entry *)_t;
    if (s->type != t->type) {
        if (s->type == DT_DIR)
            return -1;
        if (t->type == DT_DIR)
            return 1;
    }
    return strcmp(s->name, t->name);
}

static int WriteDirectory(const char *name) {
    DIR *dir = opendir(name);
    if (dir == NULL) {
        weprintf("opendir:");
        switch (errno) {
        case ENOENT:
            SetError(NOT_FOUND, "No such file or directory");
            return -1;
        case ENOTDIR:
            return WriteFile(name);
        default:
            SetError(INTERNAL_ERR, "Something went wrong");
            return -1;
        }
        /* not reached */
    }
    if (name[0] != '.') {
        SetError(INTERNAL_ERR, "Invalid path");
        return -1;
    }
    size_t npath = strlen(name);
    if (name[npath-1] != '/') {
        httpcode = TEMP_REDIR;
        Sprintf(&content, "%s/", &name[1]);
        closedir(dir);
        return -1;
    }
    httpcode = OK;
    contenttype = "text/html; charset=utf-8";
    Sprintf(&content, "<!DOCTYPE html><html><head>");
    Str tmpname = {0};
    EscapeHTML(&tmpname, name);
    Sprintf(&content, "<title>%s</title>", tmpname.arr);
    Sprintf(&content, "</head><body>");
    Sprintf(&content, "<h1>%s</h1>", tmpname.arr);
    Sprintf(&content, "<ul>");
    struct dirent *ent;
    Array(Entry) entries = {0};
    while ((ent = readdir(dir)) != NULL) {
        const char *entname = ent->d_name;
        if (entname[0] == '.' && strcmp(entname, "..") != 0)
            continue;
        ArrayPinch(&entries);
        Entry *e = &entries.arr[entries.len++];
        e->type = ent->d_type;
        if (e->type == DT_DIR) {
            tmpname.len = 0;
            Sprintf(&tmpname, "%s/", entname);
            e->name = tmpname.arr;
            ArrayZero(&tmpname);
        } else {
            e->name = estrdup(entname);
        }
    }
    qsort(entries.arr, entries.len, sizeof(Entry), &CompareFunction);
    Str tmpurl = {0};
    for (size_t i = 0; i < entries.len; i++) {
        Entry *e = &entries.arr[i];
        EscapeHTML(&tmpname, e->name);
        EncodeURL(&tmpurl, e->name);
        Sprintf(&content, "<li><a href=\"%s\">", tmpurl.arr);
        if (e->type == DT_DIR) {
            Sprintf(&content, "<b>%s</b>", tmpname.arr);
        } else {
            Sprintf(&content, "%s", tmpname.arr);
        }
        Sprintf(&content, "</a></li>");
        free(e->name);
    }
    Sprintf(&content, "</ul></body></html>");
    ArrayRelease(&entries);
    ArrayRelease(&tmpname);
    ArrayRelease(&tmpurl);
    closedir(dir);
    return 0;
}

void Respond(int cfd) {
    /* Reset variables */
    fd = cfd;
    BufReset();
    /* Read packet */
    for (int i = 0; i < 3; i++)
        PopWord(&status[i]);
    weprintf("> %s %s %s", status[0].arr, status[1].arr, status[2].arr);
    if (PopEOL() == -1) {
        SetError(BAD_REQ, "newline after status line");
        goto done;
    }
    if (strcmp(status[0].arr, "GET") != 0
        && strcmp(status[0].arr, "HEAD") != 0
        ) {
        SetError(NOT_IMPL, "method is not GET or HEAD");
        goto done;
    }
    if (!StartsWith(status[2].arr, "HTTP/1")) {
        SetError(NOT_IMPL, "HTTP version is not 1");
        goto done;
    }
    if (status[1].arr[0] != '/') {
        SetError(NOT_IMPL, "Path does not start with /");
        goto done;
    }
    if (DecodeURL(&url, status[1].arr) == -1) {
        SetError(BAD_REQ, "Invalid URL");
        goto done;
    }
    if (PopHeaders() == -1) {
        goto done;
    }
    /* Prepare response */
    ArrayPinch(&url);
    memmove(&url.arr[1], &url.arr[0], url.len);
    url.len++;
    url.arr[0] = '.';
    WriteDirectory(url.arr);
done:
    /* respond */
    weprintf("closing connection");
    Done();
    shutdown(fd, SHUT_RDWR);
    /* clear variables */
    contenttype = NULL;
    ArrayRelease(&content);
    for (size_t i = 0; i < headers.len; i++) {
        ArrayRelease(&headers.arr[i].name);
        ArrayRelease(&headers.arr[i].value);
    }
    headers.len = 0;
}
