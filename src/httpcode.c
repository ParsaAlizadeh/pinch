#include "httpcode.h"

const char *GetMessage(int status) {
    switch (status) {
    case OK:
        return "OK";
    case BAD_REQ:
        return "Bad Request";
    case NOT_IMPL:
        return "Not Implemented";
    case FORBIDDEN:
        return "Forbidden";
    case NOT_FOUND:
        return "Not Found";
    case INTERNAL_ERR:
        return "Internal Server Error";
    case TEMP_REDIR:
        return "Temporary Redirect";
    default:
        return "I Dont Know";
    }
}
