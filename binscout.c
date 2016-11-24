/*
 * Copyright (c) 2010-2016 Marc Butler <mockbutler@gmail.com>
 *
 * Search a file for a byte sequence.
 */

#define _BSD_SOURCE
#define _XOPEN_SOURCE 500

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define NUMBYTES 256

static unsigned char hex_table[] = {
        ['0'] = 0,
        ['1'] = 1,
        ['2'] = 2,
        ['3'] = 3,
        ['4'] = 4,
        ['5'] = 5,
        ['6'] = 6,
        ['7'] = 7,
        ['8'] = 8,
        ['9'] = 9,
        ['a'] = 10, ['A'] = 10,
        ['b'] = 11, ['B'] = 11,
        ['c'] = 12, ['C'] = 12,
        ['d'] = 13, ['D'] = 13,
        ['e'] = 14, ['E'] = 14,
        ['f'] = 15, ['F'] = 15,
};
#define HEXVAL(digit) (unsigned int)hex_table[(int)(digit)]

void * xmalloc0(size_t sz)
{
        void *p;
        assert(sz > 0);
        p = malloc(sz);
        if (p == NULL)
                err(1, "Exhausted memory!");
        memset(p, 0, sz);
        return p;
}

/* Byte vector. */
struct bytevec {
        unsigned int len;
        unsigned char vec[];
};

struct bytevec * compile_hex(const char *str, size_t len)
{
        unsigned int binlen, ncnt;
        struct bytevec *bvec;

        assert(str != NULL);
        assert(len > 0);

        binlen = (unsigned int)((len + 1) / 2);
        bvec = xmalloc0(sizeof(struct bytevec) + binlen);
        ncnt = 0;
        if ((len & 1) == 1)
                ncnt++;         /* Assume the first nybble is zero. */
        while (ncnt < (2 * binlen)) {
                if (!isxdigit(*str)) {
                        free(bvec);
                        return NULL;
                }
                /* This assumes that vec has been initialized to zero. */
                bvec->vec[ncnt / 2] |= (HEXVAL(*str++) * (((ncnt & 1) == 0) ? 16U : 1U));
                ncnt++;
        }
        bvec->len = binlen;
        return bvec;
}

enum endian {
        ENDIAN_LITTLE,
        ENDIAN_BIG
};

void revmem(unsigned char *buf, size_t sz)
{
        int b, e;
        char tmp;

        for (b = 0, e = sz - 1; b < e; b++, e--) {
                tmp = buf[b];
                buf[b] = buf[e];
                buf[e] = tmp;
        }
}

struct bytevec * decompose_int(uint64_t val, size_t sz, enum endian en)
{
        struct bytevec *bvec;
        int i;

        bvec = xmalloc0(sizeof(struct bytevec) + sz);
        for (i = 0; i < sz; i++)
                bvec->vec[i] = val >> (i * 8);
        if (en == ENDIAN_BIG)
                revmem(bvec->vec, sz);
        bvec->len = sz;
        return bvec;
}

/* Memory mapped file handle. */
struct mmap_file {
        union {
                void *raw;
                unsigned char *uc;
                char *c;
        } contents;
        size_t size;
};

struct mmap_file * mmap_file_ro(const char *path)
{
        int fd;
        struct stat info;
        void *contents;
        struct mmap_file *pmmf;

        assert(path != NULL);

        fd = open(path, O_RDONLY);
        if (fd < 0)
                err(1, "open %s", path);
        if (fstat(fd, &info) != 0)
                err(1, "stat %s", path);

        contents = mmap(0, (size_t)info.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (contents == MAP_FAILED)
                err(1, "mmap %s", path);
        close(fd);

        pmmf = xmalloc0(sizeof(*pmmf));
        pmmf->contents.raw = contents;
        pmmf->size = info.st_size;
        return pmmf;
}

void mmap_file_close(struct mmap_file *mmf)
{
        if (munmap(mmf->contents.raw, mmf->size) != 0)
                err(-1, "error unmapping memory mapped file");
        memset(mmf, 0, sizeof(*mmf));
}

void generate_jump_table(unsigned *jmptbl, struct bytevec *bv)
{
        int i;
        unsigned int j;

        assert(jmptbl != NULL);
        assert(bv != NULL);

        for (i = 0; i < NUMBYTES; i++)
                jmptbl[i] = bv->len;

        for (j = 0; j < bv->len; j++)
                if (jmptbl[bv->vec[j]] == bv->len)
                        jmptbl[(int)bv->vec[j]] = j + 1;
}

/* Calculate the largets byte sequence length in the tail that matches the head.
 *
 * That is: "abcxabc" would have an overlap of 3 ("abc"), and "aaa" would have
 * an overlap of 2 ("aa").
 *
 * This is used to allow overlapped sequences to be found.
 */
size_t overlap(struct bytevec *bvec)
{
        int i;
        size_t extent;
        
        assert(bvec != NULL);
        if (bvec->len == 1)
                return 0;

        for (i = 1; i < bvec->len; i++) {
                if (bvec->vec[0] == bvec->vec[i])
                        if (memcmp(&bvec->vec[0], &bvec->vec[i], bvec->len - i) == 0)
                                break;
        }
        extent = bvec->len - i;
        assert(extent < bvec->len);
        return (extent) ? extent : bvec->len;
}

void crawl(struct bytevec *bvec, struct mmap_file *mmf)
{
        static unsigned int jmptbl[NUMBYTES];
        size_t off;
        unsigned j;
        int rv;
        int match_jump;

        assert(bvec->len > 0);
        assert(mmf->size > 0);

        generate_jump_table(jmptbl, bvec);
        match_jump = bvec->len = overlap(bvec);

        rv = madvise(mmf->contents.raw, mmf->size, MADV_SEQUENTIAL);
        if (rv != 0)
                warn("madvise() failed: ");
        off = bvec->len;
        while (off < mmf->size) {
                for (j = 0; j < bvec->len; j++) {
                        if (bvec->vec[j] != mmf->contents.uc[(off - bvec->len) + j]) {
                                off += jmptbl[bvec->vec[j]];
                                break;
                        }
                }
                if (j == bvec->len) {
                        printf("%8lx\n", (unsigned long)(off - bvec->len));
                        /* Assume matches cannot overlap. */
                        off += match_jump;
                }
        }
}

enum needle_t {
        NEEDLE_HEX = 0,
        NEEDLE_STR, NEEDLE_CSTR,
        NEEDLE_LE16, NEEDLE_LE32, NEEDLE_LE64,
        NEEDLE_BE16, NEEDLE_BE32, NEEDLE_BE64
};

static char * const needle_typeids[] = {
        [NEEDLE_HEX] = "hex",
        [NEEDLE_STR] = "str",
        [NEEDLE_CSTR] = "cstr",
        [NEEDLE_LE16] = "le16",
        [NEEDLE_LE32] = "le32",
        [NEEDLE_LE64] = "le64",
        [NEEDLE_BE16] = "be16",
        [NEEDLE_BE32] = "be32",
        [NEEDLE_BE64] = "be64"
};

struct bytevec * compile_int(const char *text, size_t sz, enum endian en)
{
        assert(text != NULL);
        char *end = NULL;

        if (text[0] == '-') {
                long long int sval;
                sval = strtoll(text, &end, 0);
                return decompose_int((uint64_t)sval, sz, en);
        } else {
                unsigned long long int uval;
                uval = strtoull(text, &end, 0);
                return decompose_int((uint64_t)uval, sz, en);
        }
}

enum nul_handling {
        DROP_NUL, KEEP_NUL
};

struct bytevec * compile_str(const char *text, enum nul_handling handling)
{
        assert(text != NULL);
        struct bytevec *bvec;
        size_t len = (handling == DROP_NUL) ? strlen(text) : strlen(text) + 1;
        bvec = xmalloc0(sizeof(struct bytevec) + len);
        memcpy(&bvec->vec, text, len);
        bvec->len = len;
        return bvec;
}

struct bytevec * form_needle(enum needle_t needle_is, const char *text)
{
        struct bytevec *bvec = NULL;
        assert(text != NULL);

        switch (needle_is) {
        case NEEDLE_HEX: bvec = compile_hex(text, strlen(text)); break;
        case NEEDLE_STR: bvec = compile_str(text, DROP_NUL); break;
        case NEEDLE_CSTR: bvec = compile_str(text, KEEP_NUL); break;
        case NEEDLE_LE16: bvec = compile_int(text, 2, ENDIAN_LITTLE); break;
        case NEEDLE_LE32: bvec = compile_int(text, 4, ENDIAN_LITTLE); break;
        case NEEDLE_LE64: bvec = compile_int(text, 8, ENDIAN_LITTLE); break;
        case NEEDLE_BE16: bvec = compile_int(text, 2, ENDIAN_BIG); break;
        case NEEDLE_BE32: bvec = compile_int(text, 4, ENDIAN_BIG); break;
        case NEEDLE_BE64: bvec = compile_int(text, 8, ENDIAN_BIG); break;
        default: assert(0 && "internal error");
        }
        assert(bvec != NULL);
        return bvec;
}

void detailed_usage(void)
{
  puts("\nUsage: binscout [options] needle file\n"
       "\nSearch a binary file for the specified byte sequence.\n"
       "\nOptions:\n"
       "  -h            : This help.\n"
       "  -t <type>     : Needle type: hex, str, cstr, le16, le32, le64, be16, be32, be64\n");
}

int main(int argc, char **argv)
{
        struct bytevec *bvec;
        struct mmap_file *mmf;
        int opt, errfnd;
        char *subopts;
        char *value;
        enum needle_t needle_is = NEEDLE_HEX;

        errfnd = 0;
        while ((opt = getopt(argc, argv, "t:hBL")) != -1) {
                switch (opt) {
                case 't':
                        subopts = optarg;
                        while (*subopts != '\0' && !errfnd) {
                                switch (getsubopt(&subopts, needle_typeids, &value)) {
                                case NEEDLE_HEX: needle_is = NEEDLE_HEX; break;
                                case NEEDLE_STR: needle_is = NEEDLE_STR; break;
                                case NEEDLE_CSTR: needle_is = NEEDLE_CSTR; break;
                                case NEEDLE_LE16: needle_is = NEEDLE_LE16; break;
                                case NEEDLE_LE32: needle_is = NEEDLE_LE32; break;
                                case NEEDLE_LE64: needle_is = NEEDLE_LE64; break;
                                case NEEDLE_BE16: needle_is = NEEDLE_BE16; break;
                                case NEEDLE_BE32: needle_is = NEEDLE_BE32; break;
                                case NEEDLE_BE64: needle_is = NEEDLE_BE64; break;
                                default:
                                        err(1, "Invalid needle type.");
                                }
                        }
                        break;
                case 'h':
                        detailed_usage();
                        exit(EXIT_SUCCESS);
                default:
                        puts("\nUsage: binscout [options] needle file\n");
                        exit(EXIT_FAILURE);
                }
        }

        if ((argc - optind) < 2) {
                        puts("\nUsage: binscout [options] needle file\n");
                        exit(EXIT_FAILURE);
        }

        bvec = form_needle(needle_is, argv[optind]);
        if (bvec == NULL)
                errx(1, "Unable to parse hex '%s'", argv[optind]);
        mmf = mmap_file_ro(argv[optind + 1]);

        crawl(bvec, mmf);

        mmap_file_close(mmf);
        free(mmf);
        free(bvec);
        exit(EXIT_SUCCESS);
}
