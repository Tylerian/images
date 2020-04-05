#include "http_filter.h"

#include "module.h"

using ::weserv::api::utils::Status;

namespace weserv {
namespace nginx {

ngx_int_t check_image_too_large(ngx_event_pipe_t *p) {
    auto *r = reinterpret_cast<ngx_http_request_t *>(p->input_ctx);

    auto *lc = reinterpret_cast<ngx_weserv_loc_conf_t *>(
        ngx_http_get_module_loc_conf(r, ngx_weserv_module));

    if (p->read_length > static_cast<off_t>(lc->max_size)) {
        if (r == nullptr) {
            return NGX_ERROR;
        }

        auto *ctx = reinterpret_cast<ngx_weserv_upstream_ctx_t *>(
            ngx_http_get_module_ctx(r, ngx_weserv_module));

        if (ctx == nullptr) {
            return NGX_ERROR;
        }

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "upstream has sent an too large body: %O bytes",
                      p->read_length);

        ctx->response_status =
            Status(413,
                   "The image is too large to be downloaded. "
                   "Max image size: " +
                       std::to_string(lc->max_size) + " bytes",
                   Status::ErrorCause::Upstream);

        return NGX_ERROR;
    }

    return NGX_OK;
}

ngx_int_t ngx_weserv_copy_filter(ngx_event_pipe_t *p, ngx_buf_t *buf) {
    ngx_buf_t *b;
    ngx_chain_t *cl;

    if (buf->pos == buf->last) {
        return NGX_OK;
    }

    cl = ngx_chain_get_free_buf(p->pool, &p->free);
    if (cl == nullptr) {
        return NGX_ERROR;
    }

    b = cl->buf;

    ngx_memcpy(b, buf, sizeof(ngx_buf_t));
    b->shadow = buf;
    b->tag = p->tag;
    b->last_shadow = 1;
    b->recycled = 1;
    buf->shadow = b;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "input buf #%d", b->num);

    if (p->in) {
        *p->last_in = cl;
    } else {
        p->in = cl;
    }
    p->last_in = &cl->next;

    // Is the content length header available?
    if (p->length == -1) {
        if (check_image_too_large(p) != NGX_OK) {
            p->upstream_done = 1;

            return NGX_HTTP_REQUEST_ENTITY_TOO_LARGE;
        }

        return NGX_OK;
    }

    p->length -= b->last - b->pos;

    if (p->length == 0) {
        auto *r = reinterpret_cast<ngx_http_request_t *>(p->input_ctx);
        p->upstream_done = 1;
        r->upstream->keepalive = !r->upstream->headers_in.connection_close;

    } else if (p->length < 0) {
        auto *r = reinterpret_cast<ngx_http_request_t *>(p->input_ctx);
        p->upstream_done = 1;

        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "upstream sent more data than specified in "
                      "\"Content-Length\" header");
    }

    return NGX_OK;
}

/**
 * Reference: ngx_http_proxy_chunked_filter
 */
ngx_int_t ngx_weserv_chunked_filter(ngx_event_pipe_t *p, ngx_buf_t *buf) {
    ngx_int_t rc;
    ngx_buf_t *b, **prev;
    ngx_chain_t *cl;

    if (buf->pos == buf->last) {
        return NGX_OK;
    }

    auto *r = reinterpret_cast<ngx_http_request_t *>(p->input_ctx);
    if (r == nullptr) {
        return NGX_ERROR;
    }

    auto *ctx = reinterpret_cast<ngx_weserv_upstream_ctx_t *>(
        ngx_http_get_module_ctx(r, ngx_weserv_module));

    if (ctx == nullptr) {
        return NGX_ERROR;
    }

    b = nullptr;
    prev = &buf->shadow;

    for (;;) {
        rc = ngx_http_parse_chunked(r, buf, &ctx->chunked);

        if (rc == NGX_OK) {
            // a chunk has been parsed successfully

            cl = ngx_chain_get_free_buf(p->pool, &p->free);
            if (cl == nullptr) {
                return NGX_ERROR;
            }

            b = cl->buf;

            ngx_memzero(b, sizeof(ngx_buf_t));

            b->pos = buf->pos;
            b->start = buf->start;
            b->end = buf->end;
            b->tag = p->tag;
            b->temporary = 1;
            b->recycled = 1;

            *prev = b;
            prev = &b->shadow;

            if (p->in) {
                *p->last_in = cl;
            } else {
                p->in = cl;
            }
            p->last_in = &cl->next;

            /* STUB */ b->num = buf->num;

            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0, "input buf #%d %p",
                           b->num, b->pos);

            if (buf->last - buf->pos >= ctx->chunked.size) {
                buf->pos += (size_t)ctx->chunked.size;
                b->last = buf->pos;
                ctx->chunked.size = 0;

                continue;
            }

            ctx->chunked.size -= buf->last - buf->pos;
            buf->pos = buf->last;
            b->last = buf->last;

            continue;
        }

        if (rc == NGX_DONE) {
            // a whole response has been parsed successfully

            p->upstream_done = 1;
            r->upstream->keepalive = !r->upstream->headers_in.connection_close;

            break;
        }

        if (rc == NGX_AGAIN) {
            // set p->length, minimal amount of data we want to see

            p->length = ctx->chunked.length;

            break;
        }

        // invalid response
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "upstream sent invalid chunked response");

        return NGX_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http proxy chunked state %ui, length %O",
                   ctx->chunked.state, p->length);

    if (check_image_too_large(p) != NGX_OK) {
        p->upstream_done = 1;

        return NGX_HTTP_REQUEST_ENTITY_TOO_LARGE;
    }

    if (b != nullptr) {
        b->shadow = buf;
        b->last_shadow = 1;

        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0, "input buf %p %z",
                       b->pos, b->last - b->pos);

        return NGX_OK;
    }

    // there is no data record in the buf, add it to free chain
    if (ngx_event_pipe_add_free_buf(p, buf) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

ngx_int_t ngx_weserv_input_filter_init(void *data) {
    auto *r = reinterpret_cast<ngx_http_request_t *>(data);
    if (r == nullptr) {
        return NGX_ERROR;
    }

    auto *ctx = reinterpret_cast<ngx_weserv_upstream_ctx_t *>(
        ngx_http_get_module_ctx(r, ngx_weserv_module));

    if (ctx == nullptr) {
        return NGX_ERROR;
    }

    ngx_http_upstream_t *u = r->upstream;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "weserv upstream filter initialized s:%ui c:%d l:%O",
                   ctx->response_status.code(), u->headers_in.chunked,
                   u->headers_in.content_length_n);

    // as per RFC2616, 4.4 Message Length

    if (u->headers_in.chunked) {
        // chunked

        u->pipe->input_filter = ngx_weserv_chunked_filter;
        u->pipe->length = 3;  // "0" LF LF
    } else if (u->headers_in.content_length_n == 0) {
        // empty body: special case as filter won't be called

        u->pipe->length = 0;
        u->keepalive = !u->headers_in.connection_close;
    } else {
        // content length or connection close

        u->pipe->length = u->headers_in.content_length_n;
    }

    return NGX_OK;
}

}  // namespace nginx
}  // namespace weserv
