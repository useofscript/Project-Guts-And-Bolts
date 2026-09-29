// The account crypto the website needs, from the same Monocypher library the
// apps use, compiled to WebAssembly (see build.sh). JavaScript copies bytes
// into buf(), calls a function, and reads the answer back out of buf().
#include "monocypher.h"

// --- the few C library functions the compiler may ask for ------------------
void* memcpy(void* d, const void* s, unsigned long n) {
    unsigned char* a = d; const unsigned char* b = s;
    while (n--) *a++ = *b++;
    return d;
}
void* memset(void* d, int c, unsigned long n) {
    unsigned char* a = d;
    while (n--) *a++ = (unsigned char)c;
    return d;
}

// --- memory shared with JavaScript -----------------------------------------
// One 40 MB block: requests to sign can be big (a whole game, base64), and
// Argon2 borrows 32 MB of it (from 4 MB on) as scratch space, like the apps.
#define IO_SIZE (40u * 1024u * 1024u)
static unsigned char io[IO_SIZE];
#define WORK (io + 4u * 1024u * 1024u)

__attribute__((export_name("buf"))) unsigned char* buf(void) { return io; }
__attribute__((export_name("bufSize"))) unsigned bufSize(void) { return IO_SIZE; }

// secret[64] pub[32] <- seed[32]            layout in io: seed @0 -> secret @32, pub @96
__attribute__((export_name("keyPair"))) void keyPair(void) {
    crypto_eddsa_key_pair(io + 32, io + 96, io);
}

// sig[64] <- secret[64] @0, message @128 (len bytes); sig written @64
__attribute__((export_name("sign"))) void sign(unsigned len) {
    crypto_eddsa_sign(io + 64, io, io + 128, len);
}

// salt[16] @0, password @64 (len bytes, up to 960) -> out[64] @1024
// Argon2id, 32 MB, 3 passes: exactly Account::passwordKeys.
__attribute__((export_name("argon2"))) void argon2(unsigned len) {
    crypto_argon2_config cfg = {CRYPTO_ARGON2_ID, 32 * 1024, 3, 1};
    crypto_argon2_inputs in = {io + 64, io, len, 16};
    crypto_argon2(io + 1024, 64, WORK, cfg, in, crypto_argon2_no_extras);
    crypto_wipe(WORK, 32u * 1024u * 1024u);
}

// XChaCha20-Poly1305, like Account::backupKey / restoreKey.
// key[32] @0, nonce[24] @32, mac[16] @56, text[64] @72 -> result[64] @136
__attribute__((export_name("lock"))) void lock(void) {
    crypto_aead_lock(io + 136, io + 56, io, io + 32, 0, 0, io + 72, 64);
}
__attribute__((export_name("unlock"))) int unlock(void) {
    return crypto_aead_unlock(io + 136, io + 56, io, io + 32, 0, 0, io + 72, 64);
}

__attribute__((export_name("wipe"))) void wipe(void) { crypto_wipe(io, 1u << 20); }
