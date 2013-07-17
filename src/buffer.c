#include "buffer.h"

#define LIMIT_MAX 1024 * 1024 * 1024


static buffer_t*
alloc_buffer(void)
{
    buffer_t *buf;
    buf = (buffer_t*)PyMem_Malloc(sizeof(buffer_t));
    memset(buf, 0, sizeof(buffer_t));
    return buf;
}

static void
dealloc_buffer(buffer_t *buf)
{
    PyMem_Free(buf);
}


buffer_t*
new_buffer(size_t buf_size, size_t limit)
{
    buffer_t *buf;

    buf = alloc_buffer();
    if(buf == NULL){
        return (buffer_t*)PyErr_NoMemory();
    }

    buf->buf = PyMem_Malloc(sizeof(char) * buf_size);
    if(buf->buf == NULL){
        dealloc_buffer(buf);
        return (buffer_t*)PyErr_NoMemory();
    }

    buf->buf_size = buf_size;
    if(limit){
        buf->limit = limit;
    }else{
        buf->limit = LIMIT_MAX;
    }
    GDEBUG("new_buffer_t:%p buf_size:%d", buf, (int)buf->buf_size);
    return buf;
}

buffer_result
extend_buffer(buffer_t *buf, size_t  l)
{
    size_t newl;
    char *newbuf;
    buffer_result ret = WRITE_OK;
    newl = buf->len + l;

    if (newl >= buf->buf_size) {
        buf->buf_size *= 2;
        if(buf->buf_size <= newl) {
            buf->buf_size = (int)(buf->buf_size + newl + 1);
        }
        if(buf->buf_size > buf->limit){
            buf->buf_size = buf->limit + 1;
        }
        RDEBUG("warning !!! write2buf realloc !! %p", buf);
        newbuf = (char*)PyMem_Realloc(buf->buf, buf->buf_size);
        if (newbuf == NULL) {
            PyErr_SetString(PyExc_MemoryError,"out of memory");
            return MEMORY_ERROR;
        }
        buf->buf = newbuf;
    }
    if(newl >= buf->buf_size){
        l = buf->buf_size - buf->len -1;
        ret = LIMIT_OVER;
    }
    DEBUG("extend buf:%p len:%d", buf, (int)l);
    return ret;
}

buffer_result
write2buf(buffer_t *buf, const char *c, size_t  l)
{
    size_t newl;
    char *newbuf;
    buffer_result ret = WRITE_OK;
    newl = buf->len + l;

    if (newl >= buf->buf_size) {
        buf->buf_size *= 2;
        if(buf->buf_size <= newl) {
            buf->buf_size = (int)(buf->buf_size + newl + 1);
        }
        if(buf->buf_size > buf->limit){
            buf->buf_size = buf->limit + 1;
        }
        DEBUG("warning !!! write2buf realloc !! %p", buf);
        newbuf = (char*)PyMem_Realloc(buf->buf, buf->buf_size);
        if (newbuf == NULL) {
            PyErr_SetString(PyExc_MemoryError,"out of memory");
            return MEMORY_ERROR;
        }
        buf->buf = newbuf;
    }
    if(newl >= buf->buf_size){
        l = buf->buf_size - buf->len -1;
        ret = LIMIT_OVER;
    }
    memcpy(buf->buf + buf->len, c , l);
    buf->len += (int)l;
    DEBUG("write len:%d", (int)l);
    return ret;
}

void
free_buffer(buffer_t *buf)
{
    DEBUG("free_buffer_t %p", buf);
    PyMem_Free(buf->buf);
    dealloc_buffer(buf);
}

PyObject *
getPyString(buffer_t *buf)
{
    PyObject *o;
    o = PyBytes_FromStringAndSize(buf->buf, buf->len);
    return o;
}


PyObject *
getPyStringAndDecode(buffer_t *buf)
{
    PyObject *o;
    int l = 0;

    l = urldecode(buf->buf, buf->len);
    o = PyBytes_FromStringAndSize(buf->buf, l);
    free_buffer(buf);
    return o;
}


char *
getString(buffer_t *buf)
{
    if(buf && buf->len > 0){
        buf->buf[buf->len] = '\0';
        return buf->buf;
    }else{
        return "";
    }
}

int
copy_buffer_t(buffer_t *src, buffer_t *dest)
{
    if(write2buf(dest, src->buf, src->len) == MEMORY_ERROR){
        return -1;
    }
    return 1;
}

