/*
	------------------------------------------------------------------------------
		Licensing information can be found at the end of the file.
	------------------------------------------------------------------------------

	cute_png.h - v1.06

	To create implementation (the function definitions)
		#define CUTE_PNG_IMPLEMENTATION
	in *one* C/CPP file (translation unit) that includes this file


	SUMMARY:

		This header wraps some very nice functions by Richard Mitton from his
		tigr (Tiny Graphics) library, with some additional features and small
		bug-fixes.


	Revision history:
		1.00 (12/23/2016) initial release
		1.01 (03/08/2017) tRNS chunk support for paletted images
		1.02 (10/23/2017) support for explicitly loading paletted png images
		1.03 (11/12/2017) construct atlas in memory
		1.04 (08/23/2018) various bug fixes for filter and word decoder
		                  added `chibi_image_cp_load_blank`
		1.05 (11/10/2022) added `chibi_image_cp_save_png_to_memory`
		1.06 (02/01/2026) upgraded PNG compression from RLE to LZ77 with hash
		                  chains, providing 50-75x better compression for
		                  typical images


	EXAMPLES:

		Loading a PNG from disk, then freeing it
			chibi_image_cp_image_t img = chibi_image_cp_load_png("images/pic.png");
			...
			free(img.pix);
			CUTE_PNG_MEMSET(&img, 0, sizeof(img));

		Loading a PNG from memory, then freeing it
			chibi_image_cp_image_t img = chibi_image_cp_load_png_mem(memory, sizeof(memory));
			...
			free(img.pix);
			CUTE_PNG_MEMSET(&img, 0, sizeof(img));

		Saving a PNG to disk
			chibi_image_cp_save_png("images/example.png", &img);
			// img is just a raw RGBA buffer, and can come from anywhere,
			// not only from chibi_image_cp_load*** functions

		Creating a texture atlas in memory
			int w = 1024;
			int h = 1024;
			chibi_image_cp_atlas_image_t* imgs_out = (chibi_image_cp_atlas_image_t*)malloc(sizeof(chibi_image_cp_atlas_image_t) * my_png_count);
			chibi_image_cp_image_t atlas_img = chibi_image_cp_make_atlas(w, int h, my_png_array, my_png_count, imgs_out);
			// just pass an array of pointers to images along with the image count. Make sure to also
			// provide an array of `chibi_image_cp_atlas_image_t` for `chibi_image_cp_make_atlas` to output important UV info for the
			// images that fit into the atlas.

		Using the default atlas saver
			int errors = chibi_image_cp_default_save_atlas("atlas.png", "atlas.txt", atlas_img, atlas_imgs, img_count, names_of_all_images ? names_of_all_images : 0);
			if (errors) { ... }
			// Atlas info (like uv coordinates) are in "atlas.txt", and the image was writen to "atlas.png".
			// atlas_imgs was an array of `chibi_image_cp_atlas_image_t` from the `chibi_image_cp_make_atlas` function.

		Inflating a DEFLATE block (decompressing memory stored in DEFLATE format)
			chibi_image_cp_inflate(in, in_bytes, out, out_bytes);
			// this function requires knowledge of the un-compressed size
			// does *not* do any internal realloc! Will return errors if an
			// attempt to overwrite the out buffer is made

	CUSTOMIZATION

		There are various macros in this header you can customize by defining them before
		including cute_png.h. Simply define one to override the default behavior.

			CUTE_PNG_ALLOCA
			CUTE_PNG_ALLOC
			CUTE_PNG_FREE
			CUTE_PNG_CALLOC
			CUTE_PNG_REALLOC
			CUTE_PNG_MEMCPY
			CUTE_PNG_MEMCMP
			CUTE_PNG_MEMSET
			CUTE_PNG_ASSERT
			CUTE_PNG_FPRINTF
			CUTE_PNG_SEEK_SET
			CUTE_PNG_SEEK_END
			CUTE_PNG_FILE
			CUTE_PNG_FOPEN
			CUTE_PNG_FSEEK
			CUTE_PNG_FREAD
			CUTE_PNG_FTELL
			CUTE_PNG_FWRITE
			CUTE_PNG_FCLOSE
			CUTE_PNG_FERROR
			CUTE_PNG_ATLAS_MUST_FIT
			CUTE_PNG_ATLAS_FLIP_Y_AXIS_FOR_UV
			CUTE_PNG_ATLAS_EMPTY_COLOR
*/

/*
	Contributors:
		Zachary Carter    1.01 - bug catch for tRNS chunk in paletted images
		Dennis Korpel     1.03 - fix some pointer/memory related bugs
		Dennis Korpel     1.04 - fix for filter on first row of pixels
*/

#if !defined(CUTE_PNG_H)

#ifdef _WIN32
	#if !defined(_CRT_SECURE_NO_WARNINGS)
		#define _CRT_SECURE_NO_WARNINGS
	#endif
#endif

#ifndef CUTE_PNG_ATLAS_MUST_FIT
	#define CUTE_PNG_ATLAS_MUST_FIT            1 // returns error from chibi_image_cp_make_atlas if *any* input image does not fit
#endif // CUTE_PNG_ATLAS_MUST_FIT

#ifndef CUTE_PNG_ATLAS_FLIP_Y_AXIS_FOR_UV
	#define CUTE_PNG_ATLAS_FLIP_Y_AXIS_FOR_UV  1 // flips output uv coordinate's y. Can be useful to "flip image on load"
#endif // CUTE_PNG_ATLAS_FLIP_Y_AXIS_FOR_UV

#ifndef CUTE_PNG_ATLAS_EMPTY_COLOR
	#define CUTE_PNG_ATLAS_EMPTY_COLOR         0x000000FF // the fill color for empty areas in a texture atlas (RGBA)
#endif // CUTE_PNG_ATLAS_EMPTY_COLOR

#include <stdint.h>
#include <limits.h>

typedef struct chibi_image_cp_pixel_t chibi_image_cp_pixel_t;
typedef struct chibi_image_cp_image_t chibi_image_cp_image_t;
typedef struct chibi_image_cp_indexed_image_t chibi_image_cp_indexed_image_t;
typedef struct chibi_image_cp_atlas_image_t chibi_image_cp_atlas_image_t;

// Read this in the event of errors from any function
extern const char* chibi_image_cp_error_reason;

// return 1 for success, 0 for failures
int chibi_image_cp_inflate(void* in, int in_bytes, void* out, int out_bytes);
int chibi_image_cp_save_png(const char* file_name, const chibi_image_cp_image_t* img);

typedef struct chibi_image_cp_saved_png_t
{
	int size;   // Size of the `data` buffer.
	void* data; // Pointer to the saved png in memory.
	            // NULL if something went wrong.
	            // Call CUTE_PNG_FREE on `data` when done.
} chibi_image_cp_saved_png_t;

// Saves a png file to memory.
// Call CUTE_PNG_FREE on .data when done.
chibi_image_cp_saved_png_t chibi_image_cp_save_png_to_memory(const chibi_image_cp_image_t* img);

// Constructs an atlas image in-memory. The atlas pixels are stored in the returned image. free the pixels
// when done with them. The user must provide an array of chibi_image_cp_atlas_image_t for the `imgs` param. `imgs` holds
// information about uv coordinates for an associated image in the `pngs` array. Output image has NULL
// pixels buffer in the event of errors.
chibi_image_cp_image_t chibi_image_cp_make_atlas(int atlasWidth, int atlasHeight, const chibi_image_cp_image_t* pngs, int png_count, chibi_image_cp_atlas_image_t* imgs_out);

// A decent "default" function, ready to use out-of-the-box. Saves out an easy to parse text formatted info file
// along with an atlas image. `names` param can be optionally NULL.
int chibi_image_cp_default_save_atlas(const char* out_path_image, const char* out_path_atlas_txt, const chibi_image_cp_image_t* atlas, const chibi_image_cp_atlas_image_t* imgs, int img_count, const char** names);

// these two functions return chibi_image_cp_image_t::pix as 0 in event of errors
// call free on chibi_image_cp_image_t::pix when done, or call chibi_image_cp_free_png
chibi_image_cp_image_t chibi_image_cp_load_png(const char *file_name);
chibi_image_cp_image_t chibi_image_cp_load_png_mem(const void *png_data, int png_length);
chibi_image_cp_image_t chibi_image_cp_load_blank(int w, int h); // Alloc's pixels, but `pix` memory is uninitialized.
void chibi_image_cp_free_png(chibi_image_cp_image_t* img);
void chibi_image_cp_flip_image_horizontal(chibi_image_cp_image_t* img);

// Reads the w/h of the png without doing any other decompression or parsing.
void chibi_image_cp_load_png_wh(const void* png_data, int png_length, int* w, int* h);

// loads indexed (paletted) pngs, but does not depalette the image into RGBA pixels
// these two functions return chibi_image_cp_indexed_image_t::pix as 0 in event of errors
// call free on chibi_image_cp_indexed_image_t::pix when done, or call chibi_image_cp_free_indexed_png
chibi_image_cp_indexed_image_t chibi_image_cp_load_indexed_png(const char* file_name);
chibi_image_cp_indexed_image_t chibi_image_cp_load_indexed_png_mem(const void *png_data, int png_length);
void chibi_image_cp_free_indexed_png(chibi_image_cp_indexed_image_t* img);

// converts paletted image into a standard RGBA image
// call free on chibi_image_cp_image_t::pix when done
chibi_image_cp_image_t chibi_image_cp_depallete_indexed_image(chibi_image_cp_indexed_image_t* img);

// Pre-process the pixels to transform the image data to a premultiplied alpha format.
// Resource: http://www.essentialmath.com/GDC2015/VanVerth_Jim_DoingMathwRGB.pdf
void chibi_image_cp_premultiply(chibi_image_cp_image_t* img);

struct chibi_image_cp_pixel_t
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

struct chibi_image_cp_image_t
{
	int w;
	int h;
	chibi_image_cp_pixel_t* pix;
};

struct chibi_image_cp_indexed_image_t
{
	int w;
	int h;
	uint8_t* pix;
	uint8_t palette_len;
	chibi_image_cp_pixel_t palette[256];
};

struct chibi_image_cp_atlas_image_t
{
	int img_index;    // index into the `imgs` array
	int w, h;         // pixel w/h of original image
	float minx, miny; // u coordinate
	float maxx, maxy; // v coordinate
	int fit;          // non-zero if image fit and was placed into the atlas
};

#define CUTE_PNG_H
#endif

#ifdef CUTE_PNG_IMPLEMENTATION
#ifndef CUTE_PNG_IMPLEMENTATION_ONCE
#define CUTE_PNG_IMPLEMENTATION_ONCE

#if !defined(CUTE_PNG_ALLOCA)
	#define CUTE_PNG_ALLOCA alloca

	#ifdef _WIN32
		#include <malloc.h>
	#elif defined(__linux__)
		#include <alloca.h>
	#endif
#endif

#if !defined(CUTE_PNG_ALLOC)
	#include <stdlib.h>
	#define CUTE_PNG_ALLOC malloc
#endif

#if !defined(CUTE_PNG_FREE)
	#include <stdlib.h>
	#define CUTE_PNG_FREE free
#endif

#if !defined(CUTE_PNG_CALLOC)
	#include <stdlib.h>
	#define CUTE_PNG_CALLOC calloc
#endif

#if !defined(CUTE_PNG_REALLOC)
	#include <stdlib.h>
	#define CUTE_PNG_REALLOC realloc
#endif

#if !defined(CUTE_PNG_MEMCPY)
	#include <string.h>
	#define CUTE_PNG_MEMCPY memcpy
#endif

#if !defined(CUTE_PNG_MEMCMP)
	#include <string.h>
	#define CUTE_PNG_MEMCMP memcmp
#endif

#if !defined(CUTE_PNG_MEMSET)
	#include <string.h>
	#define CUTE_PNG_MEMSET memset
#endif

#if !defined(CUTE_PNG_ASSERT)
	#include <assert.h>
	#define CUTE_PNG_ASSERT assert
#endif

#if !defined(CUTE_PNG_FPRINTF)
	#include <stdio.h>
	#define CUTE_PNG_FPRINTF fprintf
#endif

#if !defined(CUTE_PNG_SEEK_SET)
	#include <stdio.h>
	#define CUTE_PNG_SEEK_SET SEEK_SET
#endif

#if !defined(CUTE_PNG_SEEK_END)
	#include <stdio.h>
	#define CUTE_PNG_SEEK_END SEEK_END
#endif

#if !defined(CUTE_PNG_FILE)
	#include <stdio.h>
	#define CUTE_PNG_FILE FILE
#endif

#if !defined(CUTE_PNG_FOPEN)
	#include <stdio.h>
	#define CUTE_PNG_FOPEN fopen
#endif

#if !defined(CUTE_PNG_FSEEK)
	#include <stdio.h>
	#define CUTE_PNG_FSEEK fseek
#endif

#if !defined(CUTE_PNG_FREAD)
	#include <stdio.h>
	#define CUTE_PNG_FREAD fread
#endif

#if !defined(CUTE_PNG_FTELL)
	#include <stdio.h>
	#define CUTE_PNG_FTELL ftell
#endif

#if !defined(CUTE_PNG_FWRITE)
	#include <stdio.h>
	#define CUTE_PNG_FWRITE fwrite
#endif

#if !defined(CUTE_PNG_FCLOSE)
	#include <stdio.h>
	#define CUTE_PNG_FCLOSE fclose
#endif

#if !defined(CUTE_PNG_FERROR)
	#include <stdio.h>
	#define CUTE_PNG_FERROR ferror
#endif

static chibi_image_cp_pixel_t chibi_image_cp_make_pixel_a(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	chibi_image_cp_pixel_t p;
	p.r = r; p.g = g; p.b = b; p.a = a;
	return p;
}

static chibi_image_cp_pixel_t chibi_image_cp_make_pixel(uint8_t r, uint8_t g, uint8_t b)
{
	chibi_image_cp_pixel_t p;
	p.r = r; p.g = g; p.b = b; p.a = 0xFF;
	return p;
}

const char* chibi_image_cp_error_reason;
#define CUTE_PNG_FAIL() do { goto chibi_image_cp_err; } while (0)
#define CUTE_PNG_CHECK(X, Y) do { if (!(X)) { chibi_image_cp_error_reason = Y; CUTE_PNG_FAIL(); } } while (0)
#define CUTE_PNG_CALL(X) do { if (!(X)) goto chibi_image_cp_err; } while (0)
#define CUTE_PNG_LOOKUP_BITS 9
#define CUTE_PNG_LOOKUP_COUNT (1 << CUTE_PNG_LOOKUP_BITS)
#define CUTE_PNG_LOOKUP_MASK (CUTE_PNG_LOOKUP_COUNT - 1)
#define CUTE_PNG_DEFLATE_MAX_BITLEN 15

// DEFLATE tables from RFC 1951
uint8_t chibi_image_cp_fixed_table[288 + 32] = {
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
	9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
}; // 3.2.6
uint8_t chibi_image_cp_permutation_order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 }; // 3.2.7
uint8_t chibi_image_cp_len_extra_bits[29 + 2] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,  0,0 }; // 3.2.5
uint32_t chibi_image_cp_len_base[29 + 2] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258,  0,0 }; // 3.2.5
uint8_t chibi_image_cp_dist_extra_bits[30 + 2] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,  0,0 }; // 3.2.5
uint32_t chibi_image_cp_dist_base[30 + 2] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577, 0,0 }; // 3.2.5

typedef struct chibi_image_cp_state_t
{
	uint64_t bits;
	int count;
	uint32_t* words;
	int word_count;
	int word_index;
	int bits_left;

	int final_word_available;
	uint32_t final_word;

	char* out;
	char* out_end;
	char* begin;

	uint16_t lookup[CUTE_PNG_LOOKUP_COUNT];
	uint32_t lit[288];
	uint32_t dst[32];
	uint32_t len[19];
	uint32_t nlit;
	uint32_t ndst;
	uint32_t nlen;
} chibi_image_cp_state_t;

static int chibi_image_cp_would_overflow(chibi_image_cp_state_t* s, int num_bits)
{
	return (s->bits_left + s->count) - num_bits < 0;
}

static char* chibi_image_cp_ptr(chibi_image_cp_state_t* s)
{
	CUTE_PNG_ASSERT(!(s->bits_left & 7));
	return (char*)(s->words + s->word_index) - (s->count / 8);
}

static uint64_t chibi_image_cp_peak_bits(chibi_image_cp_state_t* s, int num_bits_to_read)
{
	if (s->count < num_bits_to_read)
	{
		if (s->word_index < s->word_count)
		{
			uint32_t word = s->words[s->word_index++];
			s->bits |= (uint64_t)word << s->count;
			s->count += 32;
			CUTE_PNG_ASSERT(s->word_index <= s->word_count);
		}

		else if (s->final_word_available)
		{
			uint32_t word = s->final_word;
			s->bits |= (uint64_t)word << s->count;
			s->count += s->bits_left;
			s->final_word_available = 0;
		}
	}

	return s->bits;
}

static uint32_t chibi_image_cp_consume_bits(chibi_image_cp_state_t* s, int num_bits_to_read)
{
	CUTE_PNG_ASSERT(s->count >= num_bits_to_read);
	uint32_t bits = s->bits & (((uint64_t)1 << num_bits_to_read) - 1);
	s->bits >>= num_bits_to_read;
	s->count -= num_bits_to_read;
	s->bits_left -= num_bits_to_read;
	return bits;
}

static uint32_t chibi_image_cp_read_bits(chibi_image_cp_state_t* s, int num_bits_to_read)
{
	CUTE_PNG_ASSERT(num_bits_to_read <= 32);
	CUTE_PNG_ASSERT(num_bits_to_read >= 0);
	CUTE_PNG_ASSERT(s->bits_left > 0);
	CUTE_PNG_ASSERT(s->count <= 64);
	CUTE_PNG_ASSERT(!chibi_image_cp_would_overflow(s, num_bits_to_read));
	chibi_image_cp_peak_bits(s, num_bits_to_read);
	uint32_t bits = chibi_image_cp_consume_bits(s, num_bits_to_read);
	return bits;
}

static char* chibi_image_cp_read_file_to_memory(const char* path, int* size)
{
	char* data = 0;
	CUTE_PNG_FILE* fp = CUTE_PNG_FOPEN(path, "rb");
	int sizeNum = 0;

	if (fp)
	{
		CUTE_PNG_FSEEK(fp, 0, CUTE_PNG_SEEK_END);
		sizeNum = CUTE_PNG_FTELL(fp);
		CUTE_PNG_FSEEK(fp, 0, CUTE_PNG_SEEK_SET);
		data = (char*)CUTE_PNG_ALLOC(sizeNum + 1);
		CUTE_PNG_FREAD(data, sizeNum, 1, fp);
		data[sizeNum] = 0;
		CUTE_PNG_FCLOSE(fp);
	}

	if (size) *size = sizeNum;
	return data;
}

static uint32_t chibi_image_cp_rev16(uint32_t a)
{
	a = ((a & 0xAAAA) >>  1) | ((a & 0x5555) << 1);
	a = ((a & 0xCCCC) >>  2) | ((a & 0x3333) << 2);
	a = ((a & 0xF0F0) >>  4) | ((a & 0x0F0F) << 4);
	a = ((a & 0xFF00) >>  8) | ((a & 0x00FF) << 8);
	return a;
}

// RFC 1951 section 3.2.2
static int chibi_image_cp_build(chibi_image_cp_state_t* s, uint32_t* tree, uint8_t* lens, int sym_count)
{
	int n, codes[16], first[16], counts[16] = { 0 };

	// Frequency count
	for (n = 0; n < sym_count; n++) counts[lens[n]]++;

	// Distribute codes
	counts[0] = codes[0] = first[0] = 0;
	for (n = 1; n <= 15; ++n)
	{
		codes[n] = (codes[n - 1] + counts[n - 1]) << 1;
		first[n] = first[n - 1] + counts[n - 1];
	}

	if (s) CUTE_PNG_MEMSET(s->lookup, 0, sizeof(s->lookup));
	for (int i = 0; i < sym_count; ++i)
	{
		int len = lens[i];

		if (len != 0)
		{
			CUTE_PNG_ASSERT(len < 16);
			uint32_t code = codes[len]++;
			uint32_t slot = first[len]++;
			tree[slot] = (code << (32 - len)) | (i << 4) | len;

			if (s && len <= CUTE_PNG_LOOKUP_BITS)
			{
				int j = chibi_image_cp_rev16(code) >> (16 - len);
				while (j < (1 << CUTE_PNG_LOOKUP_BITS))
				{
					s->lookup[j] = (uint16_t)((len << CUTE_PNG_LOOKUP_BITS) | i);
					j += (1 << len);
				}
			}
		}
	}

	int max_index = first[15];
	return max_index;
}

static int chibi_image_cp_stored(chibi_image_cp_state_t* s)
{
	char* p;

	// 3.2.3
	// skip any remaining bits in current partially processed byte
	chibi_image_cp_read_bits(s, s->count & 7);

	// 3.2.4
	// read LEN and NLEN, should complement each other
	uint16_t LEN = (uint16_t)chibi_image_cp_read_bits(s, 16);
	uint16_t NLEN = (uint16_t)chibi_image_cp_read_bits(s, 16);
	CUTE_PNG_CHECK(LEN == (uint16_t)(~NLEN), "Failed to find LEN and NLEN as complements within stored (uncompressed) stream.");
	CUTE_PNG_CHECK(s->bits_left / 8 <= (int)LEN, "Stored block extends beyond end of input stream.");
	p = chibi_image_cp_ptr(s);
	CUTE_PNG_MEMCPY(s->out, p, LEN);
	s->out += LEN;
	return 1;

chibi_image_cp_err:
	return 0;
}

// 3.2.6
static int chibi_image_cp_fixed(chibi_image_cp_state_t* s)
{
	s->nlit = chibi_image_cp_build(s, s->lit, chibi_image_cp_fixed_table, 288);
	s->ndst = chibi_image_cp_build(0, s->dst, chibi_image_cp_fixed_table + 288, 32);
	return 1;
}

static int chibi_image_cp_decode(chibi_image_cp_state_t* s, uint32_t* tree, int hi)
{
	uint64_t bits = chibi_image_cp_peak_bits(s, 16);
	uint32_t search = (chibi_image_cp_rev16((uint32_t)bits) << 16) | 0xFFFF;
	int lo = 0;
	while (lo < hi)
	{
		int guess = (lo + hi) >> 1;
		if (search < tree[guess]) hi = guess;
		else lo = guess + 1;
	}

	uint32_t key = tree[lo - 1];
	uint32_t len = (32 - (key & 0xF));
	CUTE_PNG_ASSERT((search >> len) == (key >> len));

	int code = chibi_image_cp_consume_bits(s, key & 0xF);
	(void)code;
	return (key >> 4) & 0xFFF;
}

// 3.2.7
static int chibi_image_cp_dynamic(chibi_image_cp_state_t* s)
{
	uint8_t lenlens[19] = { 0 };

	int nlit = 257 + chibi_image_cp_read_bits(s, 5);
	int ndst = 1 + chibi_image_cp_read_bits(s, 5);
	int nlen = 4 + chibi_image_cp_read_bits(s, 4);

	for (int i = 0 ; i < nlen; ++i)
		lenlens[chibi_image_cp_permutation_order[i]] = (uint8_t)chibi_image_cp_read_bits(s, 3);

	// Build the tree for decoding code lengths
	s->nlen = chibi_image_cp_build(0, s->len, lenlens, 19);
	uint8_t lens[288 + 32];

	for (int n = 0; n < nlit + ndst;)
	{
		int sym = chibi_image_cp_decode(s, s->len, s->nlen);
		switch (sym)
		{
		case 16: for (int i =  3 + chibi_image_cp_read_bits(s, 2); i; --i, ++n) lens[n] = lens[n - 1]; break;
		case 17: for (int i =  3 + chibi_image_cp_read_bits(s, 3); i; --i, ++n) lens[n] = 0; break;
		case 18: for (int i = 11 + chibi_image_cp_read_bits(s, 7); i; --i, ++n) lens[n] = 0; break;
		default: lens[n++] = (uint8_t)sym; break;
		}
	}

	s->nlit = chibi_image_cp_build(s, s->lit, lens, nlit);
	s->ndst = chibi_image_cp_build(0, s->dst, lens + nlit, ndst);
	return 1;
}

// 3.2.3
static int chibi_image_cp_block(chibi_image_cp_state_t* s)
{
	while (1)
	{
		int symbol = chibi_image_cp_decode(s, s->lit, s->nlit);

		if (symbol < 256)
		{
			CUTE_PNG_CHECK(s->out + 1 <= s->out_end, "Attempted to overwrite out buffer while outputting a symbol.");
			*s->out = (char)symbol;
			s->out += 1;
		}

		else if (symbol > 256)
		{
			symbol -= 257;
			int length = chibi_image_cp_read_bits(s, chibi_image_cp_len_extra_bits[symbol]) + chibi_image_cp_len_base[symbol];
			int distance_symbol = chibi_image_cp_decode(s, s->dst, s->ndst);
			int backwards_distance = chibi_image_cp_read_bits(s, chibi_image_cp_dist_extra_bits[distance_symbol]) + chibi_image_cp_dist_base[distance_symbol];
			CUTE_PNG_CHECK(s->out - backwards_distance >= s->begin, "Attempted to write before out buffer (invalid backwards distance).");
			CUTE_PNG_CHECK(s->out + length <= s->out_end, "Attempted to overwrite out buffer while outputting a string.");
			char* src = s->out - backwards_distance;
			char* dst = s->out;
			s->out += length;

			switch (backwards_distance)
			{
			case 1: // very common in images
				CUTE_PNG_MEMSET(dst, *src, length);
				break;
			default: while (length--) *dst++ = *src++;
			}
		}

		else break;
	}

	return 1;

chibi_image_cp_err:
	return 0;
}

// 3.2.3
int chibi_image_cp_inflate(void* in, int in_bytes, void* out, int out_bytes)
{
	chibi_image_cp_state_t* s = (chibi_image_cp_state_t*)CUTE_PNG_CALLOC(1, sizeof(chibi_image_cp_state_t));
	s->bits = 0;
	s->count = 0;
	s->word_index = 0;
	s->bits_left = in_bytes * 8;

	// s->words is the in-pointer rounded up to a multiple of 4
	int first_bytes = (int) ((( (size_t) in + 3) & ~3) - (size_t) in);
	s->words = (uint32_t*)((char*)in + first_bytes);
	s->word_count = (in_bytes - first_bytes) / 4;
	int last_bytes = ((in_bytes - first_bytes) & 3);

	for (int i = 0; i < first_bytes; ++i)
		s->bits |= (uint64_t)(((uint8_t*)in)[i]) << (i * 8);

	s->final_word_available = last_bytes ? 1 : 0;
	s->final_word = 0;
	for(int i = 0; i < last_bytes; i++) 
		s->final_word |= ((uint8_t*)in)[in_bytes - last_bytes+i] << (i * 8);

	s->count = first_bytes * 8;

	s->out = (char*)out;
	s->out_end = s->out + out_bytes;
	s->begin = (char*)out;

	int count = 0;
	int bfinal;
	do
	{
		bfinal = chibi_image_cp_read_bits(s, 1);
		int btype = chibi_image_cp_read_bits(s, 2);

		switch (btype)
		{
		case 0: CUTE_PNG_CALL(chibi_image_cp_stored(s)); break;
		case 1: chibi_image_cp_fixed(s); CUTE_PNG_CALL(chibi_image_cp_block(s)); break;
		case 2: chibi_image_cp_dynamic(s); CUTE_PNG_CALL(chibi_image_cp_block(s)); break;
		case 3: CUTE_PNG_CHECK(0, "Detected unknown block type within input stream.");
		}

		++count;
	}
	while (!bfinal);

	CUTE_PNG_FREE(s);
	return 1;

chibi_image_cp_err:
	CUTE_PNG_FREE(s);
	return 0;
}

static uint8_t chibi_image_cp_paeth(uint8_t a, uint8_t b, uint8_t c)
{
	int p = a + b - c;
	int pa = abs(p - a);
	int pb = abs(p - b);
	int pc = abs(p - c);
	return (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;
}

typedef struct chibi_image_cp_lz77_state_t chibi_image_cp_lz77_state_t;

typedef struct chibi_image_cp_save_png_data_t
{
	uint32_t crc;
	uint32_t adler;
	uint32_t bits;
	int buflen;
	int bufcap;
	char* buffer;
	chibi_image_cp_lz77_state_t* lz;  // LZ77 compression state
} chibi_image_cp_save_png_data_t;

// LZ77 compression state - hash chain approach for pattern matching
typedef struct chibi_image_cp_lz77_state_t
{
	int head[4096];         // Hash table heads (-1 = no entry)
	int prev[32768];        // Chain links for positions (32K sliding window)
	uint8_t* window;        // Points to input data
	int window_size;        // Total bytes available
} chibi_image_cp_lz77_state_t;

// Hash function for 3-byte sequences - fast with good distribution
static uint32_t chibi_image_cp_lz77_hash(const uint8_t* p)
{
	return ((uint32_t)(p[0] << 8) ^ (uint32_t)(p[1] << 4) ^ (uint32_t)p[2]) & 0xFFF;
}

// Initialize LZ77 state for compression
static void chibi_image_cp_lz77_init(chibi_image_cp_lz77_state_t* lz, uint8_t* data, int size)
{
	CUTE_PNG_MEMSET(lz->head, 0xFF, sizeof(lz->head)); // -1 = no entry
	CUTE_PNG_MEMSET(lz->prev, 0xFF, sizeof(lz->prev)); // -1 = end of chain
	lz->window = data;
	lz->window_size = size;
}

// Insert position into hash chain
static void chibi_image_cp_lz77_insert(chibi_image_cp_lz77_state_t* lz, int pos)
{
	if (pos + 2 >= lz->window_size) return;
	uint32_t hash = chibi_image_cp_lz77_hash(lz->window + pos);
	int slot = pos & 0x7FFF;
	lz->prev[slot] = lz->head[hash];
	lz->head[hash] = pos;
}

// Find longest match in sliding window, returns length (0 if none, 3+ if found)
static int chibi_image_cp_lz77_find_match(chibi_image_cp_lz77_state_t* lz, int pos, int max_len, int* out_distance)
{
	uint8_t* data = lz->window;
	int best_len = 2;
	int best_dist = 0;
	int chain_count = 0;
	const int max_chain = 256;

	if (max_len < 3) return 0;
	if (max_len > 258) max_len = 258;

	uint32_t hash = chibi_image_cp_lz77_hash(data + pos);
	int match_pos = lz->head[hash];
	if (match_pos < 0) return 0;

	while (match_pos >= 0 && chain_count < max_chain)
	{
		int dist = pos - match_pos;
		if (dist > 32768 || dist <= 0) break;

		// Quick check: first and current-best bytes
		if (data[match_pos] == data[pos] && data[match_pos + best_len] == data[pos + best_len])
		{
			int len = 0;
			while (len < max_len && data[match_pos + len] == data[pos + len])
				len++;

			if (len > best_len)
			{
				best_len = len;
				best_dist = dist;
				if (len >= max_len) break;
			}
		}

		match_pos = lz->prev[match_pos & 0x7FFF];
		chain_count++;
	}

	if (best_len >= 3)
	{
		*out_distance = best_dist;
		return best_len;
	}
	return 0;
}

uint32_t chibi_image_cp_crc_table[] = {
	0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
	0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

static void chibi_image_cp_put8(chibi_image_cp_save_png_data_t* s, uint32_t a)
{
	if (s->buflen >= s->bufcap)
	{
		s->bufcap *= 2;
		s->buffer = (char*)CUTE_PNG_REALLOC(s->buffer, s->bufcap);
	}
	s->buffer[s->buflen++] = a;
	s->crc = (s->crc >> 4) ^ chibi_image_cp_crc_table[(s->crc & 15) ^ (a & 15)];
	s->crc = (s->crc >> 4) ^ chibi_image_cp_crc_table[(s->crc & 15) ^ (a >> 4)];
}

static void chibi_image_cp_update_adler(chibi_image_cp_save_png_data_t* s, uint32_t v)
{
	uint32_t s1 = s->adler & 0xFFFF;
	uint32_t s2 = (s->adler >> 16) & 0xFFFF;
	s1 = (s1 + v) % 65521;
	s2 = (s2 + s1) % 65521;
	s->adler = (s2 << 16) + s1;
}

static void chibi_image_cp_put32(chibi_image_cp_save_png_data_t* s, uint32_t v)
{
	chibi_image_cp_put8(s, (v >> 24) & 0xFF);
	chibi_image_cp_put8(s, (v >> 16) & 0xFF);
	chibi_image_cp_put8(s, (v >> 8) & 0xFF);
	chibi_image_cp_put8(s, v & 0xFF);
}

static void chibi_image_cp_put_bits(chibi_image_cp_save_png_data_t* s, uint32_t data, uint32_t bitcount)
{
	while (bitcount--)
	{
		uint32_t prev = s->bits;
		s->bits = (s->bits >> 1) | ((data & 1) << 7);
		data >>= 1;

		if (prev & 1)
		{
			chibi_image_cp_put8(s, s->bits);
			s->bits = 0x80;
		}
	}
}

static void chibi_image_cp_put_bitsr(chibi_image_cp_save_png_data_t* s, uint32_t data, uint32_t bitcount)
{
	while (bitcount--)
		chibi_image_cp_put_bits(s, data >> bitcount, 1);
}

static void chibi_image_cp_begin_chunk(chibi_image_cp_save_png_data_t* s, const char* id, uint32_t len)
{
	chibi_image_cp_put32(s, len);
	s->crc = 0xFFFFFFFF;
	chibi_image_cp_put8(s, (unsigned char)id[0]);
	chibi_image_cp_put8(s, (unsigned char)id[1]);
	chibi_image_cp_put8(s, (unsigned char)id[2]);
	chibi_image_cp_put8(s, (unsigned char)id[3]);
}

static void chibi_image_cp_encode_literal(chibi_image_cp_save_png_data_t* s, uint32_t v)
{
	// Encode a literal/length using fixed Huffman codes (RFC 1951)
	     if (v < 144) chibi_image_cp_put_bitsr(s, 0x030 + v -   0, 8);
	else if (v < 256) chibi_image_cp_put_bitsr(s, 0x190 + v - 144, 9);
	else if (v < 280) chibi_image_cp_put_bitsr(s, 0x000 + v - 256, 7);
	else              chibi_image_cp_put_bitsr(s, 0x0c0 + v - 280, 8);
}

// Encode distance using fixed Huffman (5-bit codes, reversed bit order)
static void chibi_image_cp_encode_distance(chibi_image_cp_save_png_data_t* s, int distance)
{
	int code = 0;
	for (int i = 29; i >= 0; i--)
	{
		if (distance >= (int)chibi_image_cp_dist_base[i])
		{
			code = i;
			break;
		}
	}
	chibi_image_cp_put_bitsr(s, code, 5); // Fixed distance codes are 5 bits, MSB first (reversed)
	if (chibi_image_cp_dist_extra_bits[code] > 0)
		chibi_image_cp_put_bits(s, distance - chibi_image_cp_dist_base[code], chibi_image_cp_dist_extra_bits[code]);
}

// Encode a length-distance match
static void chibi_image_cp_encode_match(chibi_image_cp_save_png_data_t* s, int length, int distance)
{
	int code = 0;
	for (int i = 28; i >= 0; i--)
	{
		if (length >= (int)chibi_image_cp_len_base[i])
		{
			code = i;
			break;
		}
	}
	chibi_image_cp_encode_literal(s, 257 + code); // Length codes start at 257
	if (chibi_image_cp_len_extra_bits[code] > 0)
		chibi_image_cp_put_bits(s, length - chibi_image_cp_len_base[code], chibi_image_cp_len_extra_bits[code]);
	chibi_image_cp_encode_distance(s, distance);
}

// Compress buffer using LZ77 with hash chain matching
static void chibi_image_cp_lz77_compress(chibi_image_cp_save_png_data_t* s, const uint8_t* data, int len)
{
	chibi_image_cp_lz77_state_t* lz = s->lz;
	chibi_image_cp_lz77_init(lz, (uint8_t*)data, len);
	int pos = 0;

	while (pos < len)
	{
		chibi_image_cp_update_adler(s, data[pos]);

		int distance;
		int match_len = chibi_image_cp_lz77_find_match(lz, pos, len - pos, &distance);

		if (match_len >= 3)
		{
			chibi_image_cp_encode_match(s, match_len, distance);
			for (int i = 0; i < match_len; i++)
			{
				if (i > 0) chibi_image_cp_update_adler(s, data[pos + i]);
				chibi_image_cp_lz77_insert(lz, pos + i);
			}
			pos += match_len;
		}
		else
		{
			chibi_image_cp_encode_literal(s, data[pos]);
			chibi_image_cp_lz77_insert(lz, pos);
			pos++;
		}
	}
}

static void chibi_image_cp_save_header(chibi_image_cp_save_png_data_t* s, chibi_image_cp_image_t* img)
{
	const unsigned char* hdr = (const unsigned char*)"\211PNG\r\n\032\n";
	for (int i = 0; i < 8; ++i) {
		chibi_image_cp_put8(s, *hdr++);
	}
	chibi_image_cp_begin_chunk(s, "IHDR", 13);
	chibi_image_cp_put32(s, img->w);
	chibi_image_cp_put32(s, img->h);
	chibi_image_cp_put8(s, 8); // bit depth
	chibi_image_cp_put8(s, 6); // RGBA
	chibi_image_cp_put8(s, 0); // compression (deflate)
	chibi_image_cp_put8(s, 0); // filter (standard)
	chibi_image_cp_put8(s, 0); // interlace off
	chibi_image_cp_put32(s, ~s->crc);
}

static void chibi_image_cp_save_data(chibi_image_cp_save_png_data_t* s, chibi_image_cp_image_t* img, long dataPos, long* dataSize)
{
	int row_bytes = 1 + img->w * 4; // filter byte + RGBA per pixel
	int total_bytes = row_bytes * img->h;
	uint8_t* filtered = (uint8_t*)CUTE_PNG_ALLOC(total_bytes);
	int pos = 0;

	// Apply Sub filter to all rows and buffer the result
	for (int y = 0; y < img->h; ++y)
	{
		chibi_image_cp_pixel_t* row = &img->pix[y * img->w];
		chibi_image_cp_pixel_t prev = chibi_image_cp_make_pixel_a(0, 0, 0, 0);

		filtered[pos++] = 1; // sub filter
		for (int x = 0; x < img->w; ++x)
		{
			filtered[pos++] = (uint8_t)(row[x].r - prev.r);
			filtered[pos++] = (uint8_t)(row[x].g - prev.g);
			filtered[pos++] = (uint8_t)(row[x].b - prev.b);
			filtered[pos++] = (uint8_t)(row[x].a - prev.a);
			prev = row[x];
		}
	}

	chibi_image_cp_begin_chunk(s, "IDAT", 0);
	chibi_image_cp_put8(s, 0x78); // zlib CMF: deflate, 32K window (deflate's max, always valid regardless of match distances)
	chibi_image_cp_put8(s, 0x01); // zlib FLG: no preset dict, FCHECK so (CMF*256 + FLG) % 31 == 0
	chibi_image_cp_put_bits(s, 3, 3); // zlib last block + fixed dictionary

	chibi_image_cp_lz77_compress(s, filtered, pos);

	chibi_image_cp_encode_literal(s, 256); // end of block
	while (s->bits != 0x80) chibi_image_cp_put_bits(s, 0, 1);
	chibi_image_cp_put32(s, s->adler);
	*dataSize = (s->buflen - dataPos) - 8;
	chibi_image_cp_put32(s, ~s->crc);

	CUTE_PNG_FREE(filtered);
}

chibi_image_cp_saved_png_t chibi_image_cp_save_png_to_memory(const chibi_image_cp_image_t* img)
{
	chibi_image_cp_saved_png_t result = { 0 };
	chibi_image_cp_save_png_data_t s = { 0 };
	chibi_image_cp_lz77_state_t* lz = NULL;
	long dataPos, dataSize, fileSize;
	if (!img) return result;

	// Allocate LZ77 state (~72KB)
	lz = (chibi_image_cp_lz77_state_t*)CUTE_PNG_ALLOC(sizeof(chibi_image_cp_lz77_state_t));
	if (!lz) return result;

	s.adler = 1;
	s.bits = 0x80;
	s.bufcap = 1024;
	s.buffer = (char*)CUTE_PNG_ALLOC(1024);
	s.lz = lz;

	chibi_image_cp_save_header(&s, (chibi_image_cp_image_t*)img);
	dataPos = s.buflen;
	chibi_image_cp_save_data(&s, (chibi_image_cp_image_t*)img, dataPos, &dataSize);

	// End chunk.
	chibi_image_cp_begin_chunk(&s, "IEND", 0);
	chibi_image_cp_put32(&s, ~s.crc);

	// Write back payload size.
	fileSize = s.buflen;
	s.buflen = dataPos;
	chibi_image_cp_put32(&s, dataSize);

	CUTE_PNG_FREE(lz);

	result.size = fileSize;
	result.data = s.buffer;
	return result;
}

int chibi_image_cp_save_png(const char* file_name, const chibi_image_cp_image_t* img)
{
	chibi_image_cp_saved_png_t s;
	long err;
	CUTE_PNG_FILE* fp = CUTE_PNG_FOPEN(file_name, "wb");
	if (!fp) return 1;
	s = chibi_image_cp_save_png_to_memory(img);
	CUTE_PNG_FWRITE(s.data, s.size, 1, fp);
	err = CUTE_PNG_FERROR(fp);
	CUTE_PNG_FCLOSE(fp);
	CUTE_PNG_FREE(s.data);
	return !err;
}

typedef struct chibi_image_cp_raw_png_t
{
	const uint8_t* p;
	const uint8_t* end;
} chibi_image_cp_raw_png_t;

static uint32_t chibi_image_cp_make32(const uint8_t* s)
{
	return (s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3];
}

static const uint8_t* chibi_image_cp_chunk(chibi_image_cp_raw_png_t* png, const char* chunk, uint32_t minlen)
{
	uint32_t len = chibi_image_cp_make32(png->p);
	const uint8_t* start = png->p;

	if (!CUTE_PNG_MEMCMP(start + 4, chunk, 4) && len >= minlen)
	{
		int offset = len + 12;

		if (png->p + offset <= png->end)
		{
			png->p += offset;
			return start + 8;
		}
	}

	return 0;
}

static const uint8_t* chibi_image_cp_find(chibi_image_cp_raw_png_t* png, const char* chunk, uint32_t minlen)
{
	const uint8_t *start;
	while (png->p < png->end)
	{
		uint32_t len = chibi_image_cp_make32(png->p);
		start = png->p;
		png->p += len + 12;

		if (!CUTE_PNG_MEMCMP(start+4, chunk, 4) && len >= minlen && png->p <= png->end)
			return start + 8;
	}

	return 0;
}

static int chibi_image_cp_unfilter(int w, int h, int bpp, uint8_t* raw)
{
	int len = w * bpp;
	uint8_t *prev;
	int x;

	if (h > 0)
	{
#define FILTER_LOOP_FIRST(A) for (x = bpp; x < len; x++) raw[x] += A; break
		switch (*raw++)
		{
		case 0: break;
		case 1: FILTER_LOOP_FIRST(raw[x - bpp]);
		case 2: break;
		case 3: FILTER_LOOP_FIRST(raw[x - bpp] / 2);
		case 4: FILTER_LOOP_FIRST(chibi_image_cp_paeth(raw[x - bpp], 0, 0));
		default: return 0;
		}
#undef FILTER_LOOP_FIRST
	}

	prev = raw;
	raw += len;

	for (int y = 1; y < h; y++, prev = raw, raw += len)
	{
#define FILTER_LOOP(A, B) for (x = 0 ; x < bpp; x++) raw[x] += A; for (; x < len; x++) raw[x] += B; break
		switch (*raw++)
		{
		case 0: break;
		case 1: FILTER_LOOP(0          , raw[x - bpp] );
		case 2: FILTER_LOOP(prev[x]    , prev[x]);
		case 3: FILTER_LOOP(prev[x] / 2, (raw[x - bpp] + prev[x]) / 2);
		case 4: FILTER_LOOP(prev[x]    , chibi_image_cp_paeth(raw[x - bpp], prev[x], prev[x -bpp]));
		default: return 0;
		}
#undef FILTER_LOOP
	}

	return 1;
}

static void chibi_image_cp_convert(int bpp, int w, int h, uint8_t* src, chibi_image_cp_pixel_t* dst)
{
	for (int y = 0; y < h; y++)
	{
		// skip filter byte
		src++;

		for (int x = 0; x < w; x++, src += bpp)
		{
			switch (bpp)
			{
				case 1: *dst++ = chibi_image_cp_make_pixel(src[0], src[0], src[0]); break;
				case 2: *dst++ = chibi_image_cp_make_pixel_a(src[0], src[0], src[0], src[1]); break;
				case 3: *dst++ = chibi_image_cp_make_pixel(src[0], src[1], src[2]); break;
				case 4: *dst++ = chibi_image_cp_make_pixel_a(src[0], src[1], src[2], src[3]); break;
			}
		}
	}
}

// http://www.libpng.org/pub/png/spec/1.2/PNG-Chunks.html#C.tRNS
static uint8_t chibi_image_cp_get_alpha_for_indexed_image(int index, const uint8_t* trns, uint32_t trns_len)
{
	if (!trns) return 255;
	else if ((uint32_t)index >= trns_len) return 255;
	else return trns[index];
}

static void chibi_image_cp_depalette(int w, int h, uint8_t* src, chibi_image_cp_pixel_t* dst, const uint8_t* plte, const uint8_t* trns, uint32_t trns_len)
{
	for (int y = 0; y < h; ++y)
	{
		// skip filter byte
		++src;

		for (int x = 0; x < w; ++x, ++src)
		{
			int c = *src;
			uint8_t r = plte[c * 3];
			uint8_t g = plte[c * 3 + 1];
			uint8_t b = plte[c * 3 + 2];
			uint8_t a = chibi_image_cp_get_alpha_for_indexed_image(c, trns, trns_len);
			*dst++ = chibi_image_cp_make_pixel_a(r, g, b, a);
		}
	}
}

static uint32_t chibi_image_cp_get_chunk_byte_length(const uint8_t* chunk)
{
	return chibi_image_cp_make32(chunk - 8);
}

static int chibi_image_cp_out_size(chibi_image_cp_image_t* img, int bpp)
{
	return (img->w + 1) * img->h * bpp;
}

chibi_image_cp_image_t chibi_image_cp_load_png_mem(const void* png_data, int png_length)
{
	const char* sig = "\211PNG\r\n\032\n";
	const uint8_t* ihdr, *first, *plte, *trns;
	int bit_depth, color_type, bpp, w, h, pix_bytes;
	int compression, filter, interlace;
	int datalen, offset;
	uint8_t* out;
	chibi_image_cp_image_t img = { 0 };
	uint8_t* data = 0;
	chibi_image_cp_raw_png_t png;
	png.p = (uint8_t*)png_data;
	png.end = (uint8_t*)png_data + png_length;

	CUTE_PNG_CHECK(!CUTE_PNG_MEMCMP(png.p, sig, 8), "incorrect file signature (is this a png file?)");
	png.p += 8;

	ihdr = chibi_image_cp_chunk(&png, "IHDR", 13);
	CUTE_PNG_CHECK(ihdr, "unable to find IHDR chunk");
	bit_depth = ihdr[8];
	color_type = ihdr[9];
	CUTE_PNG_CHECK(bit_depth == 8, "only bit-depth of 8 is supported");

	switch (color_type)
	{
		case 0: bpp = 1; break; // greyscale
		case 2: bpp = 3; break; // RGB
		case 3: bpp = 1; break; // paletted
		case 4: bpp = 2; break; // grey+alpha
		case 6: bpp = 4; break; // RGBA
		default: CUTE_PNG_CHECK(0, "unknown color type");
	}

	// +1 for filter byte (which is dumb! just stick this at file header...)
	w = chibi_image_cp_make32(ihdr) + 1;
	h = chibi_image_cp_make32(ihdr + 4);
	CUTE_PNG_CHECK(w >= 1, "invalid IHDR chunk found, image width was less than 1");
	CUTE_PNG_CHECK(h >= 1, "invalid IHDR chunk found, image height was less than 1");
        CUTE_PNG_CHECK((int64_t) w * h * sizeof(chibi_image_cp_pixel_t) < INT_MAX, "image too large");
	pix_bytes = w * h * sizeof(chibi_image_cp_pixel_t);
	img.w = w - 1;
	img.h = h;
	img.pix = (chibi_image_cp_pixel_t*)CUTE_PNG_ALLOC(pix_bytes);
	CUTE_PNG_CHECK(img.pix, "unable to allocate raw image space");

	compression = ihdr[10];
	filter = ihdr[11];
	interlace = ihdr[12];
	CUTE_PNG_CHECK(!compression, "only standard compression DEFLATE is supported");
	CUTE_PNG_CHECK(!filter, "only standard adaptive filtering is supported");
	CUTE_PNG_CHECK(!interlace, "interlacing is not supported");

	// PLTE must come before any IDAT chunk
	first = png.p;
	plte = chibi_image_cp_find(&png, "PLTE", 0);
	if (!plte) png.p = first;
	else first = png.p;

	// tRNS can come after PLTE
	trns = chibi_image_cp_find(&png, "tRNS", 0);
	if (!trns) png.p = first;
	else first = png.p;

	// Compute length of the DEFLATE stream through IDAT chunk data sizes
	datalen = 0;
	for (const uint8_t* idat = chibi_image_cp_find(&png, "IDAT", 0); idat; idat = chibi_image_cp_chunk(&png, "IDAT", 0))
	{
		uint32_t len = chibi_image_cp_get_chunk_byte_length(idat);
		datalen += len;
	}

	// Copy in IDAT chunk data sections to form the compressed DEFLATE stream
	png.p = first;
	data = (uint8_t*)CUTE_PNG_ALLOC(datalen);
	offset = 0;
	for (const uint8_t* idat = chibi_image_cp_find(&png, "IDAT", 0); idat; idat = chibi_image_cp_chunk(&png, "IDAT", 0))
	{
		uint32_t len = chibi_image_cp_get_chunk_byte_length(idat);
		CUTE_PNG_MEMCPY(data + offset, idat, len);
		offset += len;
	}

	// check for proper zlib structure in DEFLATE stream
	CUTE_PNG_CHECK(data && datalen >= 6, "corrupt zlib structure in DEFLATE stream");
	CUTE_PNG_CHECK((data[0] & 0x0f) == 0x08, "only zlib compression method (RFC 1950) is supported");
	CUTE_PNG_CHECK((data[0] & 0xf0) <= 0x70, "innapropriate window size detected");
	CUTE_PNG_CHECK(!(data[1] & 0x20), "preset dictionary is present and not supported");

	// check for integer overflow
	CUTE_PNG_CHECK(chibi_image_cp_out_size(&img, 4) >= 1, "invalid image size found");
	CUTE_PNG_CHECK(chibi_image_cp_out_size(&img, bpp) >= 1, "invalid image size found");

	out = (uint8_t*)img.pix + chibi_image_cp_out_size(&img, 4) - chibi_image_cp_out_size(&img, bpp);
	CUTE_PNG_CHECK(chibi_image_cp_inflate(data + 2, datalen - 6, out, pix_bytes), "DEFLATE algorithm failed");
	CUTE_PNG_CHECK(chibi_image_cp_unfilter(img.w, img.h, bpp, out), "invalid filter byte found");

	if (color_type == 3)
	{
		CUTE_PNG_CHECK(plte, "color type of indexed requires a PLTE chunk");
		uint32_t trns_len = trns ? chibi_image_cp_get_chunk_byte_length(trns) : 0;
		chibi_image_cp_depalette(img.w, img.h, out, img.pix, plte, trns, trns_len);
	}
	else chibi_image_cp_convert(bpp, img.w, img.h, out, img.pix);

	CUTE_PNG_FREE(data);
	return img;

chibi_image_cp_err:
	CUTE_PNG_FREE(data);
	CUTE_PNG_FREE(img.pix);
	img.pix = 0;

	return img;
}

chibi_image_cp_image_t chibi_image_cp_load_blank(int w, int h)
{
	chibi_image_cp_image_t img;
	img.w = w;
	img.h = h;
	img.pix = (chibi_image_cp_pixel_t*)CUTE_PNG_ALLOC(w * h * sizeof(chibi_image_cp_pixel_t));
	return img;
}

chibi_image_cp_image_t chibi_image_cp_load_png(const char *file_name)
{
	chibi_image_cp_image_t img = { 0 };
	int len;
	void* data = chibi_image_cp_read_file_to_memory(file_name, &len);
	if (!data) return img;
	img = chibi_image_cp_load_png_mem(data, len);
	CUTE_PNG_FREE(data);
	return img;
}

void chibi_image_cp_free_png(chibi_image_cp_image_t* img)
{
	CUTE_PNG_FREE(img->pix);
	img->pix = 0;
	img->w = img->h = 0;
}

void chibi_image_cp_flip_image_horizontal(chibi_image_cp_image_t* img)
{
	chibi_image_cp_pixel_t* pix = img->pix;
	int w = img->w;
	int h = img->h;
	int flips = h / 2;
	for (int i = 0; i < flips; ++i)
	{
		chibi_image_cp_pixel_t* a = pix + w * i;
		chibi_image_cp_pixel_t* b = pix + w * (h - i - 1);
		for (int j = 0; j < w; ++j)
		{
			chibi_image_cp_pixel_t t = *a;
			*a = *b;
			*b = t;
			++a;
			++b;
		}
	}
}

void chibi_image_cp_load_png_wh(const void* png_data, int png_length, int* w_out, int* h_out)
{
	const char* sig = "\211PNG\r\n\032\n";
	const uint8_t* ihdr;
	chibi_image_cp_raw_png_t png;
	int w, h;
	png.p = (uint8_t*)png_data;
	png.end = (uint8_t*)png_data + png_length;

	if (w_out) *w_out = 0;
	if (h_out) *h_out = 0;

	CUTE_PNG_CHECK(!CUTE_PNG_MEMCMP(png.p, sig, 8), "incorrect file signature (is this a png file?)");
	png.p += 8;

	ihdr = chibi_image_cp_chunk(&png, "IHDR", 13);
	CUTE_PNG_CHECK(ihdr, "unable to find IHDR chunk");

	// +1 for filter byte (which is dumb! just stick this at file header...)
	w = chibi_image_cp_make32(ihdr) + 1;
	h = chibi_image_cp_make32(ihdr + 4);
	if (w_out) *w_out = w - 1;
	if (h_out) *h_out = h;

	chibi_image_cp_err:;
}

chibi_image_cp_indexed_image_t chibi_image_cp_load_indexed_png(const char* file_name)
{
	chibi_image_cp_indexed_image_t img = { 0 };
	int len;
	void* data = chibi_image_cp_read_file_to_memory(file_name, &len);
	if (!data) return img;
	img = chibi_image_cp_load_indexed_png_mem(data, len);
	CUTE_PNG_FREE(data);
	return img;
}

static void chibi_image_cp_unpack_indexed_rows(int w, int h, uint8_t* src, uint8_t* dst)
{
	for (int y = 0; y < h; ++y)
	{
		// skip filter byte
		++src;

		for (int x = 0; x < w; ++x, ++src)
		{
			*dst++ = *src;
		}
	}
}

void chibi_image_cp_unpack_palette(chibi_image_cp_pixel_t* dst, const uint8_t* plte, int plte_len, const uint8_t* trns, int trns_len)
{
	for (int i = 0; i < plte_len * 3; i += 3)
	{
		unsigned char r = plte[i];
		unsigned char g = plte[i + 1];
		unsigned char b = plte[i + 2];
		unsigned char a = chibi_image_cp_get_alpha_for_indexed_image(i / 3, trns, trns_len);
		chibi_image_cp_pixel_t p = chibi_image_cp_make_pixel_a(r, g, b, a);
		*dst++ = p;
	}
}

chibi_image_cp_indexed_image_t chibi_image_cp_load_indexed_png_mem(const void *png_data, int png_length)
{
	const char* sig = "\211PNG\r\n\032\n";
	const uint8_t* ihdr, *first, *plte, *trns;
	int bit_depth, color_type, bpp, w, h, pix_bytes;
	int compression, filter, interlace;
	int datalen, offset;
	int plte_len;
	uint8_t* out;
	chibi_image_cp_indexed_image_t img = { 0 };
	uint8_t* data = 0;
	chibi_image_cp_raw_png_t png;
	png.p = (uint8_t*)png_data;
	png.end = (uint8_t*)png_data + png_length;

	CUTE_PNG_CHECK(!CUTE_PNG_MEMCMP(png.p, sig, 8), "incorrect file signature (is this a png file?)");
	png.p += 8;

	ihdr = chibi_image_cp_chunk(&png, "IHDR", 13);
	CUTE_PNG_CHECK(ihdr, "unable to find IHDR chunk");
	bit_depth = ihdr[8];
	color_type = ihdr[9];
	bpp = 1; // bytes per pixel
	CUTE_PNG_CHECK(bit_depth == 8, "only bit-depth of 8 is supported");
	CUTE_PNG_CHECK(color_type == 3, "only indexed png images (images with a palette) are valid for chibi_image_cp_load_indexed_png_mem");

	// +1 for filter byte (which is dumb! just stick this at file header...)
	w = chibi_image_cp_make32(ihdr) + 1;
	h = chibi_image_cp_make32(ihdr + 4);
        CUTE_PNG_CHECK((int64_t) w * h * sizeof(uint8_t) < INT_MAX, "image too large");
	pix_bytes = w * h * sizeof(uint8_t);
	img.w = w - 1;
	img.h = h;
	img.pix = (uint8_t*)CUTE_PNG_ALLOC(pix_bytes);
	CUTE_PNG_CHECK(img.pix, "unable to allocate raw image space");

	compression = ihdr[10];
	filter = ihdr[11];
	interlace = ihdr[12];
	CUTE_PNG_CHECK(!compression, "only standard compression DEFLATE is supported");
	CUTE_PNG_CHECK(!filter, "only standard adaptive filtering is supported");
	CUTE_PNG_CHECK(!interlace, "interlacing is not supported");

	// PLTE must come before any IDAT chunk
	first = png.p;
	plte = chibi_image_cp_find(&png, "PLTE", 0);
	if (!plte) png.p = first;
	else first = png.p;

	// tRNS can come after PLTE
	trns = chibi_image_cp_find(&png, "tRNS", 0);
	if (!trns) png.p = first;
	else first = png.p;

	// Compute length of the DEFLATE stream through IDAT chunk data sizes
	datalen = 0;
	for (const uint8_t* idat = chibi_image_cp_find(&png, "IDAT", 0); idat; idat = chibi_image_cp_chunk(&png, "IDAT", 0))
	{
		uint32_t len = chibi_image_cp_get_chunk_byte_length(idat);
		datalen += len;
	}

	// Copy in IDAT chunk data sections to form the compressed DEFLATE stream
	png.p = first;
	data = (uint8_t*)CUTE_PNG_ALLOC(datalen);
	offset = 0;
	for (const uint8_t* idat = chibi_image_cp_find(&png, "IDAT", 0); idat; idat = chibi_image_cp_chunk(&png, "IDAT", 0))
	{
		uint32_t len = chibi_image_cp_get_chunk_byte_length(idat);
		CUTE_PNG_MEMCPY(data + offset, idat, len);
		offset += len;
	}

	// check for proper zlib structure in DEFLATE stream
	CUTE_PNG_CHECK(data && datalen >= 6, "corrupt zlib structure in DEFLATE stream");
	CUTE_PNG_CHECK((data[0] & 0x0f) == 0x08, "only zlib compression method (RFC 1950) is supported");
	CUTE_PNG_CHECK((data[0] & 0xf0) <= 0x70, "innapropriate window size detected");
	CUTE_PNG_CHECK(!(data[1] & 0x20), "preset dictionary is present and not supported");

	out = img.pix;
	CUTE_PNG_CHECK(chibi_image_cp_inflate(data + 2, datalen - 6, out, pix_bytes), "DEFLATE algorithm failed");
	CUTE_PNG_CHECK(chibi_image_cp_unfilter(img.w, img.h, bpp, out), "invalid filter byte found");
	chibi_image_cp_unpack_indexed_rows(img.w, img.h, out, img.pix);

	plte_len = chibi_image_cp_get_chunk_byte_length(plte) / 3;
	chibi_image_cp_unpack_palette(img.palette, plte, plte_len, trns, chibi_image_cp_get_chunk_byte_length(trns));
	img.palette_len = (uint8_t)plte_len;

	CUTE_PNG_FREE(data);
	return img;

chibi_image_cp_err:
	CUTE_PNG_FREE(data);
	CUTE_PNG_FREE(img.pix);
	img.pix = 0;

	return img;
}

void chibi_image_cp_free_indexed_png(chibi_image_cp_indexed_image_t* img)
{
	CUTE_PNG_FREE(img->pix);
	img->pix = 0;
	img->w = img->h = 0;
}

chibi_image_cp_image_t chibi_image_cp_depallete_indexed_image(chibi_image_cp_indexed_image_t* img)
{
	chibi_image_cp_image_t out = { 0 };
	out.w = img->w;
	out.h = img->h;
	out.pix = (chibi_image_cp_pixel_t*)CUTE_PNG_ALLOC(sizeof(chibi_image_cp_pixel_t) * out.w * out.h);

	chibi_image_cp_pixel_t* dst = out.pix;
	uint8_t* src = img->pix;

	for (int y = 0; y < out.h; ++y)
	{
		for (int x = 0; x < out.w; ++x)
		{
			int index = *src++;
			chibi_image_cp_pixel_t p = img->palette[index];
			*dst++ = p;
		}
	}

	return out;
}

typedef struct chibi_image_cp_v2i_t
{
	int x;
	int y;
} chibi_image_cp_v2i_t;

typedef struct chibi_image_cp_integer_image_t
{
	int img_index;
	chibi_image_cp_v2i_t size;
	chibi_image_cp_v2i_t min;
	chibi_image_cp_v2i_t max;
	int fit;
} chibi_image_cp_integer_image_t;

static chibi_image_cp_v2i_t chibi_image_cp_v2i(int x, int y)
{
	chibi_image_cp_v2i_t v;
	v.x = x;
	v.y = y;
	return v;
}

static chibi_image_cp_v2i_t chibi_image_cp_sub(chibi_image_cp_v2i_t a, chibi_image_cp_v2i_t b)
{
	chibi_image_cp_v2i_t v;
	v.x = a.x - b.x;
	v.y = a.y - b.y;
	return v;
}

static chibi_image_cp_v2i_t chibi_image_cp_add(chibi_image_cp_v2i_t a, chibi_image_cp_v2i_t b)
{
	chibi_image_cp_v2i_t v;
	v.x = a.x + b.x;
	v.y = a.y + b.y;
	return v;
}

typedef struct chibi_image_cp_atlas_node_t
{
	chibi_image_cp_v2i_t size;
	chibi_image_cp_v2i_t min;
	chibi_image_cp_v2i_t max;
} chibi_image_cp_atlas_node_t;

static chibi_image_cp_atlas_node_t* chibi_image_cp_best_fit(int sp, const chibi_image_cp_image_t* png, chibi_image_cp_atlas_node_t* nodes)
{
	int bestVolume = INT_MAX;
	chibi_image_cp_atlas_node_t *best_node = 0;
	int width = png->w;
	int height = png->h;
	int png_volume = width * height;

	for (int i = 0; i < sp; ++i)
	{
		chibi_image_cp_atlas_node_t *node = nodes + i;
		int can_contain = node->size.x >= width && node->size.y >= height;
		if (can_contain)
		{
			int node_volume = node->size.x * node->size.y;
			if (node_volume == png_volume) return node;
			if (node_volume < bestVolume)
			{
				bestVolume = node_volume;
				best_node = node;
			}
		}
	}

	return best_node;
}

static int chibi_image_cp_perimeter_pred(chibi_image_cp_integer_image_t* a, chibi_image_cp_integer_image_t* b)
{
	int perimeterA = 2 * (a->size.x + a->size.y);
	int perimeterB = 2 * (b->size.x + b->size.y);
	return perimeterB < perimeterA;
}

void chibi_image_cp_premultiply(chibi_image_cp_image_t* img)
{
	int w = img->w;
	int h = img->h;
	int stride = w * sizeof(chibi_image_cp_pixel_t);
	uint8_t* data = (uint8_t*)img->pix;

	for(int i = 0; i < (int)stride * h; i += sizeof(chibi_image_cp_pixel_t))
	{
		float a = (float)data[i + 3] / 255.0f;
		float r = (float)data[i + 0] / 255.0f;
		float g = (float)data[i + 1] / 255.0f;
		float b = (float)data[i + 2] / 255.0f;
		r *= a;
		g *= a;
		b *= a;
		data[i + 0] = (uint8_t)(r * 255.0f);
		data[i + 1] = (uint8_t)(g * 255.0f);
		data[i + 2] = (uint8_t)(b * 255.0f);
	}
}

static void chibi_image_cp_qsort(chibi_image_cp_integer_image_t* items, int count)
{
	if (count <= 1) return;

	chibi_image_cp_integer_image_t pivot = items[count - 1];
	int low = 0;
	for (int i = 0; i < count - 1; ++i)
	{
		if (chibi_image_cp_perimeter_pred(items + i, &pivot))
		{
			chibi_image_cp_integer_image_t tmp = items[i];
			items[i] = items[low];
			items[low] = tmp;
			low++;
		}
	}

	items[count - 1] = items[low];
	items[low] = pivot;
	chibi_image_cp_qsort(items, low);
	chibi_image_cp_qsort(items + low + 1, count - 1 - low);
}

static void chibi_image_cp_write_pixel(char* mem, long color) {
	mem[0] = (color >> 24) & 0xFF;
	mem[1] = (color >> 16) & 0xFF;
	mem[2] = (color >>  8) & 0xFF;
	mem[3] = (color >>  0) & 0xFF;
}

chibi_image_cp_image_t chibi_image_cp_make_atlas(int atlas_width, int atlas_height, const chibi_image_cp_image_t* pngs, int png_count, chibi_image_cp_atlas_image_t* imgs_out)
{
	float w0, h0, div, wTol, hTol;
	int atlas_image_size, atlas_stride, sp;
	void* atlas_pixels = 0;
	int atlas_node_capacity = png_count * 2;
	chibi_image_cp_image_t atlas_image;
	chibi_image_cp_integer_image_t* images = 0;
	chibi_image_cp_atlas_node_t* nodes = 0;

	atlas_image.w = atlas_width;
	atlas_image.h = atlas_height;
	atlas_image.pix = 0;

	CUTE_PNG_CHECK(pngs, "pngs array was NULL");
	CUTE_PNG_CHECK(imgs_out, "imgs_out array was NULL");

	images = (chibi_image_cp_integer_image_t*)CUTE_PNG_ALLOCA(sizeof(chibi_image_cp_integer_image_t) * png_count);
	nodes = (chibi_image_cp_atlas_node_t*)CUTE_PNG_ALLOC(sizeof(chibi_image_cp_atlas_node_t) * atlas_node_capacity);
	CUTE_PNG_CHECK(images, "out of mem");
	CUTE_PNG_CHECK(nodes, "out of mem");

	for (int i = 0; i < png_count; ++i)
	{
		const chibi_image_cp_image_t* png = pngs + i;
		chibi_image_cp_integer_image_t* image = images + i;
		image->fit = 0;
		image->size = chibi_image_cp_v2i(png->w, png->h);
		image->img_index = i;
	}

	// Sort PNGs from largest to smallest
	chibi_image_cp_qsort(images, png_count);

	// stack pointer, the stack is the nodes array which we will
	// allocate nodes from as necessary.
	sp = 1;

	nodes[0].min = chibi_image_cp_v2i(0, 0);
	nodes[0].max = chibi_image_cp_v2i(atlas_width, atlas_height);
	nodes[0].size = chibi_image_cp_v2i(atlas_width, atlas_height);

	// Nodes represent empty space in the atlas. Placing a texture into the
	// atlas involves splitting a node into two smaller pieces (or, if a
	// perfect fit is found, deleting the node).
	for (int i = 0; i < png_count; ++i)
	{
		chibi_image_cp_integer_image_t* image = images + i;
		const chibi_image_cp_image_t* png = pngs + image->img_index;
		int width = png->w;
		int height = png->h;
		chibi_image_cp_atlas_node_t *best_fit = chibi_image_cp_best_fit(sp, png, nodes);
		if (CUTE_PNG_ATLAS_MUST_FIT) CUTE_PNG_CHECK(best_fit, "Not enough room to place image in atlas.");
		else if (!best_fit) 
		{
			image->fit = 0;
			continue;
		}

		image->min = best_fit->min;
		image->max = chibi_image_cp_add(image->min, image->size);

		if (best_fit->size.x == width && best_fit->size.y == height)
		{
			chibi_image_cp_atlas_node_t* last_node = nodes + --sp;
			*best_fit = *last_node;
			image->fit = 1;

			continue;
		}

		image->fit = 1;

		if (sp == atlas_node_capacity)
		{
			int new_capacity = atlas_node_capacity * 2;
			chibi_image_cp_atlas_node_t* new_nodes = (chibi_image_cp_atlas_node_t*)CUTE_PNG_ALLOC(sizeof(chibi_image_cp_atlas_node_t) * new_capacity);
			CUTE_PNG_CHECK(new_nodes, "out of mem");
			CUTE_PNG_MEMCPY(new_nodes, nodes, sizeof(chibi_image_cp_atlas_node_t) * sp);

			// best_fit became a dangling pointer, so relocate it
			best_fit = new_nodes + (best_fit - nodes);
			CUTE_PNG_FREE(nodes);

			nodes = new_nodes;
			atlas_node_capacity = new_capacity;
		}

		chibi_image_cp_atlas_node_t* new_node = nodes + sp++;
		new_node->min = best_fit->min;

		// Split bestFit along x or y, whichever minimizes
		// fragmentation of empty space
		chibi_image_cp_v2i_t d = chibi_image_cp_sub(best_fit->size, chibi_image_cp_v2i(width, height));
		if (d.x < d.y)
		{
			new_node->size.x = d.x;
			new_node->size.y = height;
			new_node->min.x += width;

			best_fit->size.y = d.y;
			best_fit->min.y += height;
		}

		else
		{
			new_node->size.x = width;
			new_node->size.y = d.y;
			new_node->min.y += height;

			best_fit->size.x = d.x;
			best_fit->min.x += width;
		}

		new_node->max = chibi_image_cp_add(new_node->min, new_node->size);
	}

	// Write the final atlas image, use CUTE_PNG_ATLAS_EMPTY_COLOR as base color
	atlas_stride = atlas_width * sizeof(chibi_image_cp_pixel_t);
	atlas_image_size = atlas_width * atlas_height * sizeof(chibi_image_cp_pixel_t);
	atlas_pixels = CUTE_PNG_ALLOC(atlas_image_size);
	CUTE_PNG_CHECK(atlas_pixels, "out of mem");
	
	for(int i = 0; i < atlas_image_size; i += sizeof(chibi_image_cp_pixel_t)) {
		chibi_image_cp_write_pixel((char*)atlas_pixels + i, CUTE_PNG_ATLAS_EMPTY_COLOR);
	}

	for (int i = 0; i < png_count; ++i)
	{
		chibi_image_cp_integer_image_t* image = images + i;

		if (image->fit)
		{
			const chibi_image_cp_image_t* png = pngs + image->img_index;
			char* pixels = (char*)png->pix;
			chibi_image_cp_v2i_t min = image->min;
			chibi_image_cp_v2i_t max = image->max;
			int atlas_offset = min.x * sizeof(chibi_image_cp_pixel_t);
			int tex_stride = png->w * sizeof(chibi_image_cp_pixel_t);

			for (int row = min.y, y = 0; row < max.y; ++row, ++y)
			{
				void* row_ptr = (char*)atlas_pixels + (row * atlas_stride + atlas_offset);
				CUTE_PNG_MEMCPY(row_ptr, pixels + y * tex_stride, tex_stride);
			}
		}
	}

	atlas_image.pix = (chibi_image_cp_pixel_t*)atlas_pixels;

	// squeeze UVs inward by 128th of a pixel
	// this prevents atlas bleeding. tune as necessary for good results.
	w0 = 1.0f / (float)(atlas_width);
	h0 = 1.0f / (float)(atlas_height);
	div = 1.0f / 128.0f;
	wTol = w0 * div;
	hTol = h0 * div;

	for (int i = 0; i < png_count; ++i)
	{
		chibi_image_cp_integer_image_t* image = images + i;
		chibi_image_cp_atlas_image_t* img_out = imgs_out + i;

		img_out->img_index = image->img_index;
		img_out->w = image->size.x;
		img_out->h = image->size.y;
		img_out->fit = image->fit;

		if (image->fit)
		{
			chibi_image_cp_v2i_t min = image->min;
			chibi_image_cp_v2i_t max = image->max;

			float min_x = (float)min.x * w0 + wTol;
			float min_y = (float)min.y * h0 + hTol;
			float max_x = (float)max.x * w0 - wTol;
			float max_y = (float)max.y * h0 - hTol;

			// flip image on y axis
			if (CUTE_PNG_ATLAS_FLIP_Y_AXIS_FOR_UV)
			{
				float tmp = min_y;
				min_y = max_y;
				max_y = tmp;
			}

			img_out->minx = min_x;
			img_out->miny = min_y;
			img_out->maxx = max_x;
			img_out->maxy = max_y;
		}
	}

	CUTE_PNG_FREE(nodes);
	return atlas_image;

chibi_image_cp_err:
	CUTE_PNG_FREE(atlas_pixels);
	CUTE_PNG_FREE(nodes);
	atlas_image.pix = 0;
	return atlas_image;
}

int chibi_image_cp_default_save_atlas(const char* out_path_image, const char* out_path_atlas_txt, const chibi_image_cp_image_t* atlas, const chibi_image_cp_atlas_image_t* imgs, int img_count, const char** names)
{
	CUTE_PNG_FILE* fp = CUTE_PNG_FOPEN(out_path_atlas_txt, "wt");
	CUTE_PNG_CHECK(fp, "unable to open out_path_atlas_txt in chibi_image_cp_default_save_atlas");

	CUTE_PNG_FPRINTF(fp, "%s\n%d\n\n", out_path_image, img_count);

	for (int i = 0; i < img_count; ++i)
	{
		const chibi_image_cp_atlas_image_t* image = imgs + i;
		const char* name = names ? names[image->img_index] : 0;

		if (image->fit)
		{
			int width = image->w;
			int height = image->h;
			float min_x = image->minx;
			float min_y = image->miny;
			float max_x = image->maxx;
			float max_y = image->maxy;

			if (name) CUTE_PNG_FPRINTF(fp, "{ \"%s\", w = %d, h = %d, u = { %.10f, %.10f }, v = { %.10f, %.10f } }\n", name, width, height, min_x, min_y, max_x, max_y);
			else CUTE_PNG_FPRINTF(fp, "{ w = %d, h = %d, u = { %.10f, %.10f }, v = { %.10f, %.10f } }\n", width, height, min_x, min_y, max_x, max_y);
		}
	}

	// Save atlas image PNG to disk
	CUTE_PNG_CHECK(chibi_image_cp_save_png(out_path_image, atlas), "failed to save atlas image to disk");

chibi_image_cp_err:
	CUTE_PNG_FCLOSE(fp);
	return 0;
}

#endif // CUTE_PNG_IMPLEMENTATION_ONCE
#endif // CUTE_PNG_IMPLEMENTATION

/*
	------------------------------------------------------------------------------
	This software is available under 2 licenses - you may choose the one you like.
	------------------------------------------------------------------------------
	ALTERNATIVE A - zlib license
	Copyright (c) 2026 Randy Gaul https://randygaul.github.io
	This software is provided 'as-is', without any express or implied warranty.
	In no event will the authors be held liable for any damages arising from
	the use of this software.
	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:
	  1. The origin of this software must not be misrepresented; you must not
	     claim that you wrote the original software. If you use this software
	     in a product, an acknowledgment in the product documentation would be
	     appreciated but is not required.
	  2. Altered source versions must be plainly marked as such, and must not
	     be misrepresented as being the original software.
	  3. This notice may not be removed or altered from any source distribution.
	------------------------------------------------------------------------------
	ALTERNATIVE B - Public Domain (www.unlicense.org)
	This is free and unencumbered software released into the public domain.
	Anyone is free to copy, modify, publish, use, compile, sell, or distribute this 
	software, either in source code form or as a compiled binary, for any purpose, 
	commercial or non-commercial, and by any means.
	In jurisdictions that recognize copyright laws, the author or authors of this 
	software dedicate any and all copyright interest in the software to the public 
	domain. We make this dedication for the benefit of the public at large and to 
	the detriment of our heirs and successors. We intend this dedication to be an 
	overt act of relinquishment in perpetuity of all present and future rights to 
	this software under copyright law.
	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
	AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
	ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
	WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
	------------------------------------------------------------------------------
*/
