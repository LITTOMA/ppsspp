#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zstd.h"

enum {
	ZIM_FORMAT_MASK = 15,
	ZIM_HAS_MIPS = 16,
	ZIM_ZSTD_COMPRESSED = 4096,
};

static uint32_t read_le32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
	p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint8_t *read_file(const char *path, size_t *size_out) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fprintf(stderr, "seek %s failed\n", path);
		fclose(f);
		return NULL;
	}
	long size = ftell(f);
	if (size < 0) {
		fprintf(stderr, "tell %s failed\n", path);
		fclose(f);
		return NULL;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fprintf(stderr, "rewind %s failed\n", path);
		fclose(f);
		return NULL;
	}
	uint8_t *data = (uint8_t *)malloc((size_t)size);
	if (!data) {
		fprintf(stderr, "alloc %ld bytes failed\n", size);
		fclose(f);
		return NULL;
	}
	if (fread(data, 1, (size_t)size, f) != (size_t)size) {
		fprintf(stderr, "read %s failed\n", path);
		free(data);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*size_out = (size_t)size;
	return data;
}

static int write_file(const char *path, const uint8_t *data, size_t size) {
	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
		return 0;
	}
	if (fwrite(data, 1, size, f) != size) {
		fprintf(stderr, "write %s failed\n", path);
		fclose(f);
		return 0;
	}
	fclose(f);
	return 1;
}

static int raw_zim_size(uint32_t width, uint32_t height, uint32_t flags, size_t *size_out) {
	const uint32_t format = flags & ZIM_FORMAT_MASK;
	uint32_t bpp;
	switch (format) {
	case 0:
		bpp = 4;
		break;
	case 1:
	case 2:
		bpp = 2;
		break;
	default:
		fprintf(stderr, "unsupported ZIM format %u\n", format);
		return 0;
	}

	uint32_t levels = 1;
	if (flags & ZIM_HAS_MIPS) {
		uint32_t min_dim = width < height ? width : height;
		levels = 1;
		while (min_dim > 1) {
			min_dim >>= 1;
			++levels;
		}
	}

	size_t total = 0;
	for (uint32_t i = 0; i < levels; ++i) {
		if (!width || !height) {
			fprintf(stderr, "invalid mip dimensions at level %u\n", i);
			return 0;
		}
		total += (size_t)width * (size_t)height * bpp;
		width >>= 1;
		height >>= 1;
	}
	*size_out = total;
	return 1;
}

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: %s input.zim output.zim\n", argv[0]);
		return 2;
	}

	size_t input_size = 0;
	uint8_t *input = read_file(argv[1], &input_size);
	if (!input) {
		return 1;
	}
	if (input_size < 16 || memcmp(input, "ZIMG", 4) != 0) {
		fprintf(stderr, "%s is not a ZIM file\n", argv[1]);
		free(input);
		return 1;
	}

	const uint32_t width = read_le32(input + 4);
	const uint32_t height = read_le32(input + 8);
	const uint32_t flags = read_le32(input + 12);
	size_t raw_size = 0;
	if (!raw_zim_size(width, height, flags, &raw_size)) {
		free(input);
		return 1;
	}

	if (!(flags & ZIM_ZSTD_COMPRESSED)) {
		int ok = write_file(argv[2], input, input_size);
		free(input);
		return ok ? 0 : 1;
	}

	uint8_t *output = (uint8_t *)malloc(16 + raw_size);
	if (!output) {
		fprintf(stderr, "alloc %zu bytes failed\n", 16 + raw_size);
		free(input);
		return 1;
	}
	memcpy(output, input, 16);
	write_le32(output + 12, flags & ~((uint32_t)ZIM_ZSTD_COMPRESSED));

	const size_t decoded = ZSTD_decompress(output + 16, raw_size, input + 16, input_size - 16);
	if (ZSTD_isError(decoded)) {
		fprintf(stderr, "zstd decode %s failed: %s\n", argv[1], ZSTD_getErrorName(decoded));
		free(output);
		free(input);
		return 1;
	}
	if (decoded != raw_size) {
		fprintf(stderr, "zstd decode %s produced %zu bytes, expected %zu\n", argv[1], decoded, raw_size);
		free(output);
		free(input);
		return 1;
	}

	int ok = write_file(argv[2], output, 16 + raw_size);
	free(output);
	free(input);
	return ok ? 0 : 1;
}
