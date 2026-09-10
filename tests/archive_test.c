/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "resource_image.h"
#include <assert.h>
#include <boot/archive.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct boot_archive archive;
static void known_hash(const char *data, const char *expected)
{
    uint8_t out[32];
    char text[65];
    boot_sha256(data, strlen(data), out);
    for (unsigned i = 0; i < 32; ++i)
        sprintf(text + i * 2, "%02x", out[i]);
    assert(!strcmp(text, expected));
}
int main(int argc, char **argv)
{
    if (argc == 2)
    {
        FILE *file = fopen(argv[1], "rb");
        assert(file && !fseek(file, 0, SEEK_END));
        long length = ftell(file);
        assert(length >= 0 && length <= BOOT_ARCHIVE_MAX_SIZE);
        rewind(file);
        uint8_t *data = malloc((size_t)length + 1);
        assert(data && fread(data, 1, (size_t)length, file) == (size_t)length);
        fclose(file);
        boot_status_t s = boot_archive_open(&archive, data, (size_t)length);
        if (!s)
            s = boot_archive_verify(&archive);
        free(data);
        return s ? 1 : 0;
    }
    known_hash("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    known_hash("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    known_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    size_t size = sizeof(boot_resource_image);
    assert(!boot_archive_open(&archive, boot_resource_image, size));
    assert(archive.count == 5 && !boot_archive_verify(&archive));
    struct boot_archive_file f;
    assert(!boot_archive_find(&archive, "boot.lua", &f) && f.size);
    size_t config = (size_t)(f.data - boot_resource_image);
    assert(boot_archive_find(&archive, "missing", &f) && !f.data);
    uint8_t *copy = malloc(size);
    assert(copy);
    /* Every truncation before the complete trailer must fail; zero trailing
       block padding is optional. Exact allocations make ASan catch overreads. */
    size_t end = size;
    while (end && !boot_resource_image[end - 1])
        --end;
    end = (end + 1 + 3) & ~(size_t)3;
    for (size_t n = 0; n < end; ++n)
    {
        uint8_t *short_data = malloc(n ? n : 1);
        assert(short_data);
        memcpy(short_data, boot_resource_image, n);
        assert(boot_archive_open(&archive, short_data, n) && !archive.count);
        free(short_data);
    }
    memcpy(copy, boot_resource_image, size);
    copy[config] ^= 1;
    assert(!boot_archive_open(&archive, copy, size) && boot_archive_verify(&archive));
    uint32_t state = 0x1987;
    for (unsigned i = 0; i < 10000; ++i)
    {
        memcpy(copy, boot_resource_image, size);
        state = state * 1664525 + 1013904223;
        size_t where = state % size;
        copy[where] ^= (uint8_t)((state >> 24) | 1);
        if (!boot_archive_open(&archive, copy, size))
            (void)boot_archive_verify(&archive);
        else
            assert(!archive.count);
    }
    assert(boot_archive_open(&archive, copy, BOOT_ARCHIVE_MAX_SIZE + 1));
    assert(boot_archive_open(&archive, NULL, 1));
    free(copy);
    puts("PASS: archive bounds, truncation, manifest, SHA-256 and 10000 mutations");
    return 0;
}
