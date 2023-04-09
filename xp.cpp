//
// Created by Andrew on 09/04/2023.
//

#include "header.h"

/* Unpacks the Product Key. */
void unpackXP(ul32 *serial, ul32 *hash, ul32 *sig, ul32 *raw) {

    // We're assuming that the quantity of information within the product key is at most 114 bits.
    // log2(24^25) = 114.

    // Serial = Bits [0..30] -> 31 bits
	serial[0] = raw[0] & 0x7fffffff;
 
    // Hash (e) = Bits [31..58] -> 28 bits
	hash[0] = ((raw[0] >> 31) | (raw[1] << 1)) & 0xfffffff;
 
    // Signature (s) = Bits [59..113] -> 55 bits
	sig[0] = (raw[1] >> 27) | (raw[2] << 5);
	sig[1] = (raw[2] >> 27) | (raw[3] << 5);
}

/* Repacks the Product Key. */
void packXP(ul32 *raw, ul32 *serial, ul32 *hash, ul32 *sig) {
	raw[0] = serial[0] | ((hash[0] & 1) << 31);
	raw[1] = (hash[0] >> 1) | ((sig[0] & 0x1f) << 27);
	raw[2] = (sig[0] >> 5) | (sig[1] << 27);
	raw[3] = sig[1] >> 5;
}

/* Verify Product Key */
void verifyXPKey(EC_GROUP *eCurve, EC_POINT *generator, EC_POINT *publicKey, char *cdKey) {
	byte pKey[PK_LENGTH];

	BN_CTX *context = BN_CTX_new();

    // Remove dashes from the CD-pKey.
	for (int i = 0, k = 0; i < strlen(cdKey) && k < PK_LENGTH; i++) {
		for (int j = 0; j < PK_LENGTH - 1; j++) {
			if (cdKey[i] != '-' && cdKey[i] == charset[j]) {
                pKey[k++] = j;
				break;
			}
		}

        if (k >= PK_LENGTH) break;
	}
	
    // Convert Base24 CD-pKey to bytecode.
	ul32 bKey[4]{};
	ul32 pID[1], hash[1], sig[2];

	unbase24(bKey, pKey);
 
    // Output CD-pKey bytecode.
	printf("Bytecode: %.8lX %.8lX %.8lX %.8lX\n", bKey[3], bKey[2], bKey[1], bKey[0]);

    // Extract data, hash and signature from the bytecode.
    unpackXP(pID, hash, sig, bKey);
    printProductID(pID);
	
	printf("PID: %.8lX\nHash: %.8lX\nSignature: %.8lX %.8lX\n", pID[0], hash[0], sig[1], sig[0]);

    // e = Hash
    // s = Signature
	BIGNUM *e, *s;

    // Put hash word into BigNum e.
	e = BN_new();
	BN_set_word(e, hash[0]);

    // Reverse signature and create a new BigNum s.
    endiannessConvert((unsigned char *) sig, sizeof(sig));
	s = BN_bin2bn((unsigned char *)sig, sizeof(sig), nullptr);

    // Create x and y.
	BIGNUM *x = BN_new();
	BIGNUM *y = BN_new();

    // Create 2 new points on the existing elliptic curve.
	EC_POINT *u = EC_POINT_new(eCurve);
	EC_POINT *v = EC_POINT_new(eCurve);

    // EC_POINT_mul calculates r = generator * n + q * m.
	// v = s * generator + e * (-publicKey)

    // u = generator * s
	EC_POINT_mul(eCurve, u, nullptr, generator, s, context);

    // v = publicKey * e
	EC_POINT_mul(eCurve, v, nullptr, publicKey, e, context);

    // v += u
	EC_POINT_add(eCurve, v, u, v, context);

    // EC_POINT_get_affine_coordinates() sets x and y, either of which may be NULL, to the corresponding coordinates of p.
    // x = v.x; y = v.y;
	EC_POINT_get_affine_coordinates(eCurve, v, x, y, context);


	byte buf[FIELD_BYTES], md[SHA_DIGEST_LENGTH], t[4];
	ul32 h;

	SHA_CTX hContext;

    // h = First32(SHA-1(pID || v.x || v.y)) >> 4
	SHA1_Init(&hContext);

    // Chop Product ID into 4 bytes.
	t[0] = pID[0] & 0xff;                   // First 8 bits
	t[1] = (pID[0] & 0xff00) >> 8;          // Second 8 bits
	t[2] = (pID[0] & 0xff0000) >> 16;       // Third 8 bits
	t[3] = (pID[0] & 0xff000000) >> 24;     // Fourth 8 bits

    // Hash chunk of data.
	SHA1_Update(&hContext, t, sizeof(t));

    // Empty buffer, place v.x in little-endiannessConvert.
	memset(buf, 0, sizeof(buf));
	BN_bn2bin(x, buf);
    endiannessConvert((unsigned char *) buf, sizeof(buf));

    // Hash chunk of data.
	SHA1_Update(&hContext, buf, sizeof(buf));

    // Empty buffer, place v.y in little-endiannessConvert.
	memset(buf, 0, sizeof(buf));
	BN_bn2bin(y, buf);
    endiannessConvert((unsigned char *) buf, sizeof(buf));

    // Hash chunk of data.
	SHA1_Update(&hContext, buf, sizeof(buf));

    // Store the final message from hContext in md.
	SHA1_Final(md, &hContext);

    // h = First32(SHA-1(pID || v.x || v.y)) >> 4
	h = (md[0] | (md[1] << 8) | (md[2] << 16) | (md[3] << 24)) >> 4;
	h &= 0xfffffff;
	
	printf("Calculated hash: %.8lX\n", h);

    // If we managed to generateXPKey a pKey with the same hash, the pKey is correct.
	if (h == hash[0]) cprintf("Key valid\n", 0x0A);
	else cprintf("Key invalid\n", 0x0C);

	putchar('\n');
	
	BN_free(e);
	BN_free(s);
	BN_free(x);
	BN_free(y);

    BN_CTX_free(context);

	EC_POINT_free(u);
	EC_POINT_free(v);
}

/* Generate a valid Product Key. */
void generateXPKey(byte *pKey, EC_GROUP *eCurve, EC_POINT *generator, BIGNUM *order, BIGNUM *privateKey, ul32 *pRaw) {
    EC_POINT *r = EC_POINT_new(eCurve);
    BN_CTX *ctx = BN_CTX_new();

	BIGNUM *c = BN_new();
	BIGNUM *s = BN_new();
	BIGNUM *x = BN_new();
	BIGNUM *y = BN_new();

	ul32 bKey[4]{};

	do {
        memset(bKey, 0, 4);

        // Generate a random number c consisting of 384 bits without any constraints.
		BN_rand(c, FIELD_BITS, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY);

        // r = generator * c;
		EC_POINT_mul(eCurve, r, nullptr, generator, c, ctx);

        // x = r.x; y = r.y;
		EC_POINT_get_affine_coordinates(eCurve, r, x, y, ctx);
		
		SHA_CTX hContext;
		byte md[SHA_DIGEST_LENGTH], buf[FIELD_BYTES], t[4];
		ul32 hash[1];

        // h = (First-32(SHA1(pRaw, r.x, r.y)) >> 4
		SHA1_Init(&hContext);

        // Chop Raw Product Key into 4 bytes.
		t[0] = pRaw[0] & 0xff;
		t[1] = (pRaw[0] & 0xff00) >> 8;
		t[2] = (pRaw[0] & 0xff0000) >> 16;
		t[3] = (pRaw[0] & 0xff000000) >> 24;

        // Hash chunk of data.
		SHA1_Update(&hContext, t, sizeof(t));

        // Empty buffer, place r.x in little-endiannessConvert.
		memset(buf, 0, sizeof(buf));
		BN_bn2bin(x, buf);
        endiannessConvert((unsigned char *) buf, sizeof(buf));

        // Hash chunk of data.
		SHA1_Update(&hContext, buf, sizeof(buf));

        // Empty buffer, place r.y in little-endiannessConvert.
		memset(buf, 0, sizeof(buf));
		BN_bn2bin(y, buf);
        endiannessConvert((unsigned char *) buf, sizeof(buf));

        // Hash chunk of data.
		SHA1_Update(&hContext, buf, sizeof(buf));

        // Store the final message from hContext in md.
		SHA1_Final(md, &hContext);

        // h = (First-32(SHA1(pRaw, r.x, r.y)) >> 4
		hash[0] = (md[0] | (md[1] << 8) | (md[2] << 16) | (md[3] << 24)) >> 4;
		hash[0] &= 0xfffffff;
		
		/* s = privateKey * hash + c; */
        // s = privateKey;
		BN_copy(s, privateKey);

        // s *= hash;
        BN_mul_word(s, hash[0]);

        // BN_mod_add() adds a to b % m and places the non-negative result in r.
        // s = |s + c % order|;
		BN_mod_add(s, s, c, order, ctx);

        // Convert s from BigNum back to bytecode and reverse the endianness.
		ul32 sig[2]{};

		BN_bn2bin(s, (byte *)sig);
        endiannessConvert((byte *) sig, BN_num_bytes(s));

        // Pack product key.
        packXP(bKey, pRaw, hash, sig);

        printf("PID: %.8lX\nHash: %.8lX\nSignature: %.8lX %.8lX\n\n", pRaw[0], hash[0], sig[1], sig[0]);
	} while (bKey[3] >= 0x40000);
    // ↑ ↑ ↑
    // bKey[3] can't be longer than 18 bits, else the signature part will make
    // the CD-key longer than 25 characters.

    // Convert the key to Base24.
	base24(pKey, bKey);
	
	BN_free(c);
	BN_free(s);
	BN_free(x);
	BN_free(y);

    BN_CTX_free(ctx);
	EC_POINT_free(r);
}