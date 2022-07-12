#include <x86intrin.h>
#include <stdint.h>

static const uint8_t shuffle_masks[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x8f, 0x8e, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x88, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x80,
};

static void shiftr128(__m128i in, size_t n, __m128i *outl, __m128i *outr) {
    const __m128i ma = _mm_loadu_si128((const __m128i *)(shuffle_masks + (16 - n)));
    const __m128i mb = _mm_xor_si128(ma, _mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()));

    *outl = _mm_shuffle_epi8(in, mb);
    *outr = _mm_shuffle_epi8(in, ma);
}

uint64_t crc64(const uint8_t *data, size_t length) {
    uint64_t crc = 0;
    const uint64_t k1 = 0xe05dd497ca393ae4;
    const uint64_t k2 = 0xdabe95afc7875f40;
    const uint64_t mu = 0x9c3e466c172963d5;
    const uint64_t p  = 0x92d8af2baf0e1e85;

    const __m128i fc1 = _mm_set_epi64x(k2, k1);
    const __m128i fc2 = _mm_set_epi64x(p, mu);

    const uint8_t *end = data + length;

    const __m128i *aligned_data = (const __m128i *)((uintptr_t) data & ~(uintptr_t) 15);
    const __m128i *aligned_end = (const __m128i *)(((uintptr_t) end + 15) & ~(uintptr_t) 15);

    const size_t lead_size = data - (const uint8_t *) aligned_data;
    const size_t lead_out_size = (const uint8_t *) aligned_end - end;

    const __m128i lead_mask = _mm_loadu_si128((const __m128i *)(shuffle_masks + (16 - lead_size)));
    const __m128i data0 = _mm_blendv_epi8(_mm_setzero_si128(), _mm_load_si128(aligned_data), lead_mask);

    const __m128i icrc = _mm_set_epi64x(0, ~crc);

    __m128i crc0, crc1;
    shiftr128(icrc, 16 - length, &crc0, &crc1);

    __m128i A, B;
    shiftr128(data0, lead_out_size, &A, &B);

    const __m128i P = _mm_xor_si128(A, crc0);
    __m128i R = _mm_xor_si128(_mm_clmulepi64_si128(P, fc1, 0x10), _mm_xor_si128(_mm_srli_si128(P, 8), _mm_slli_si128(crc1, 8)));

    const __m128i T1 = _mm_clmulepi64_si128(R, fc2, 0x00);
    const __m128i T2 = _mm_xor_si128(_mm_xor_si128(_mm_clmulepi64_si128(T1, fc2, 0x10), _mm_slli_si128(T1, 8)), R);

    return ~(((uint64_t)(uint32_t)_mm_extract_epi32(T2, 3) << 32) | (uint64_t)(uint32_t)_mm_extract_epi32(T2, 2));
}
