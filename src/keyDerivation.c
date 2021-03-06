#include <os_io_seproxyhal.h>
#include <stdint.h>

#include "assert.h"
#include "errors.h"
#include "keyDerivation.h"
#include "stream.h"
#include "cbor.h"
#include "hash.h"
#include "crc32.h"
#include "base58.h"
#include "utils.h"

#define VALIDATE_PARAM(cond) if (!(cond)) THROW(ERR_INVALID_REQUEST_PARAMETERS)

void validatePathForPrivateKeyDerivation(const path_spec_t* pathSpec)
{
	uint32_t bip44 = BIP_44 | HARDENED_BIP32;
	uint32_t adaCoinType = ADA_COIN_TYPE | HARDENED_BIP32;

	VALIDATE_PARAM(pathSpec->length >= 3 && pathSpec->length <= 10);

	VALIDATE_PARAM(pathSpec->path[0] ==  bip44);
	VALIDATE_PARAM(pathSpec->path[1] ==  adaCoinType);
	VALIDATE_PARAM(pathSpec->path[2] >= HARDENED_BIP32);
}

void derivePrivateKey(
        const path_spec_t* pathSpec,
        chain_code_t* chainCode,
        privateKey_t* privateKey
)
{
	validatePathForPrivateKeyDerivation(pathSpec);

	uint8_t privateKeyRawBuffer[64];

	STATIC_ASSERT(SIZEOF(chainCode->code) == 32, __bad_length);
	os_memset(chainCode->code, 0, SIZEOF(chainCode->code));

	BEGIN_TRY {
		TRY {
			STATIC_ASSERT(CX_APILEVEL >= 5, unsupported_api_level);
			STATIC_ASSERT(SIZEOF(privateKey->d) == 64, __bad_length);

			os_perso_derive_node_bip32(
			        CX_CURVE_Ed25519,
			        pathSpec->path,
			        pathSpec->length,
			        privateKeyRawBuffer,
			        chainCode->code);

			// We should do cx_ecfp_init_private_key here, but it does not work in SDK < 1.5.4,
			// should work with the new SDK
			privateKey->curve = CX_CURVE_Ed25519;
			privateKey->d_len = 64;
			os_memmove(privateKey->d, privateKeyRawBuffer, 64);
		}
		FINALLY {
			os_memset(privateKeyRawBuffer, 0, SIZEOF(privateKeyRawBuffer));
		}
	} END_TRY;
}


void deriveRawPublicKey(
        const privateKey_t* privateKey,
        cx_ecfp_public_key_t* publicKey
)
{
	// We should do cx_ecfp_generate_pair here, but it does not work in SDK < 1.5.4,
	// should work with the new SDK
	cx_eddsa_get_public_key(
	        // cx_eddsa has a special case struct for Cardano's private keys
	        // but signature is standard
	        (const struct cx_ecfp_256_private_key_s *) privateKey,
	        CX_SHA512,
	        publicKey,
	        NULL, 0, NULL, 0);
}

void extractRawPublicKey(
        const cx_ecfp_public_key_t* publicKey,
        uint8_t* outBuffer, size_t outSize
)
{
	// copy public key little endian to big endian
	ASSERT(outSize == 32);
	STATIC_ASSERT(SIZEOF(publicKey->W) == 65, __bad_length);

	uint8_t i;
	for (i = 0; i < 32; i++) {
		outBuffer[i] = publicKey->W[64 - i];
	}

	if ((publicKey->W[32] & 1) != 0) {
		outBuffer[31] |= 0x80;
	}
}

static void validatePathForAddressDerivation(const path_spec_t* pathSpec)
{
	// other checks are when deriving private key
	VALIDATE_PARAM(pathSpec->length >= 5);
	VALIDATE_PARAM(pathSpec->path[3] == 0 || pathSpec->path[3] == 1);
}


// pub_key + chain_code
void deriveExtendedPublicKey(
        const path_spec_t* pathSpec,
        extendedPublicKey_t* out
)
{
	privateKey_t privateKey;
	chain_code_t chainCode;

	STATIC_ASSERT(SIZEOF(*out) == CHAIN_CODE_SIZE + PUBLIC_KEY_SIZE, __bad_size_1);

	BEGIN_TRY {
		TRY {
			derivePrivateKey(
			        pathSpec,
			        &chainCode,
			        &privateKey
			);

			// Pubkey part
			cx_ecfp_public_key_t publicKey;

			deriveRawPublicKey(&privateKey, &publicKey);

			STATIC_ASSERT(SIZEOF(out->pubKey) == PUBLIC_KEY_SIZE, __bad_size_2);

			extractRawPublicKey(&publicKey, out->pubKey, SIZEOF(out->pubKey));

			// Chain code (we copy it second to avoid mid-updates extractRawPublicKey throws
			STATIC_ASSERT(CHAIN_CODE_SIZE == SIZEOF(out->chainCode), __bad_size_3);
			STATIC_ASSERT(CHAIN_CODE_SIZE == SIZEOF(chainCode.code), __bad_size_4);
			os_memmove(out->chainCode, chainCode.code, CHAIN_CODE_SIZE);
		}
		FINALLY {
			os_memset(&privateKey, 0, SIZEOF(privateKey));
		}
	} END_TRY;
}


// Note(ppershing): updates ptr!
#define WRITE_TOKEN(ptr, end, type, value) \
	{ \
	    ptr += cbor_writeToken(type, value, ptr, end - ptr); \
	}

// Note(ppershing): updates ptr!
#define WRITE_DATA(ptr, end, buf, bufSize) \
	{ \
	    ASSERT(bufSize < BUFFER_SIZE_PARANOIA); \
	    if (ptr + bufSize > end) THROW(ERR_DATA_TOO_LARGE); \
	    os_memmove(ptr, buf, bufSize); \
	    ptr += bufSize; \
	}

void addressRootFromExtPubKey(
        const extendedPublicKey_t* extPubKey,
        uint8_t* outBuffer, size_t outSize
)
{
	ASSERT(SIZEOF(*extPubKey) == EXTENDED_PUBKEY_SIZE);
	ASSERT(outSize == 28);

	uint8_t cborBuffer[64 + 10];
	uint8_t* ptr = cborBuffer;
	uint8_t* end = END(cborBuffer);


	// [0, [0, publicKey:chainCode], Map(0)]
	// TODO(ppershing): what are the first two 0 constants?
	WRITE_TOKEN(ptr, end, CBOR_TYPE_ARRAY, 3);
	WRITE_TOKEN(ptr, end, CBOR_TYPE_UNSIGNED, 0);

	// enter inner array
	WRITE_TOKEN(ptr, end, CBOR_TYPE_ARRAY, 2);
	WRITE_TOKEN(ptr, end, CBOR_TYPE_UNSIGNED, 0);
	WRITE_TOKEN(ptr, end, CBOR_TYPE_BYTES, EXTENDED_PUBKEY_SIZE);
	WRITE_DATA(ptr, end, extPubKey, EXTENDED_PUBKEY_SIZE);
	// exit inner array

	WRITE_TOKEN(ptr, end, CBOR_TYPE_MAP, 0);

	// cborBuffer is hashed twice. First by sha3_256 and then by blake2b_224
	uint8_t cborShaHash[32];
	sha3_256_hash(
	        cborBuffer, ptr - cborBuffer,
	        cborShaHash, SIZEOF(cborShaHash)
	);
	blake2b_224_hash(
	        cborShaHash, SIZEOF(cborShaHash),
	        outBuffer, outSize
	);
}

uint32_t deriveAddress(
        const path_spec_t* pathSpec,
        uint8_t* outBuffer, size_t outSize
)
{
	validatePathForAddressDerivation(pathSpec);

	uint8_t addressRoot[28];
	{
		extendedPublicKey_t extPubKey;

		deriveExtendedPublicKey(pathSpec, &extPubKey);

		addressRootFromExtPubKey(
		        &extPubKey,
		        addressRoot, SIZEOF(addressRoot)
		);
	}

	uint8_t addressRaw[100];
	uint8_t* ptr = BEGIN(addressRaw);
	uint8_t* end = END(addressRaw);

	// [tag(24), bytes(33 - { [bytes(28 - { rootAddress } ), map(0), 0] }), checksum]
	// 1st level
	WRITE_TOKEN(ptr, end, CBOR_TYPE_ARRAY, 2);
	WRITE_TOKEN(ptr, end, CBOR_TYPE_TAG, 24);

	const uint64_t INNER_SIZE= 33;
	// Note(ppershing): here we expect we know the serialization
	// length. For now it is constant and we save some stack space
	// this way but in the future we might need to refactor this code
	WRITE_TOKEN(ptr, end, CBOR_TYPE_BYTES, INNER_SIZE);

	uint8_t* inner_begin = ptr;
	{
		// 2nd level
		WRITE_TOKEN(ptr, end, CBOR_TYPE_ARRAY, 3);

		WRITE_TOKEN(ptr, end, CBOR_TYPE_BYTES, SIZEOF(addressRoot));
		WRITE_DATA(ptr, end, addressRoot, SIZEOF(addressRoot));

		WRITE_TOKEN(ptr, end, CBOR_TYPE_MAP, 0);

		WRITE_TOKEN(ptr, end, CBOR_TYPE_UNSIGNED, 0); // TODO(what does this zero stand for?)

		// Note(ppershing): see note above
		uint8_t* inner_end = ptr;
		ASSERT(inner_end - inner_begin == INNER_SIZE);
	}
	uint32_t checksum = crc32(inner_begin, INNER_SIZE);

	// back to 1st level
	WRITE_TOKEN(ptr, end, CBOR_TYPE_UNSIGNED, checksum);

	uint32_t length = encode_base58(
	                          addressRaw, ptr - addressRaw,
	                          outBuffer, outSize
	                  );
	return length;
}
