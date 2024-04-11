#ifndef HTTPCODE_H
#define HTTPCODE_H

enum {
    OK = 200,
    TEMP_REDIR = 307,
    BAD_REQ = 400,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    INTERNAL_ERR = 500,
    NOT_IMPL = 501,
};

extern const char *GetMessage(int status);

#endif
