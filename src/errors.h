#ifndef H_CARDANO_APP_ERRORS
#define H_CARDANO_APP_ERRORS

// exception codes
#define SW_DEVELOPER_ERR 0x6B00
#define SW_INVALID_PARAM 0x6B01
#define SW_IMPROPER_INIT 0x6B02

// Still in some call

#define SW_USER_REJECTED 0x6985

enum {
	// Successfull responses
	SUCCESS                 = 0x9000,

	// Start of error which trigger automatic response
	// Note that any such error will reset
	// multi-APDU exchange
	_ERR_AUTORESPOND_START         = 0x6E00,

	// Bad request header
	ERR_MALFORMED_REQUEST_HEADER   = 0x6E01,
	// Unknown CLA
	ERR_BAD_CLA                    = 0x6E02,
	// Unknown INS
	ERR_UNKNOWN_INS                = 0x6E03,
	// TODO(should we move this to ERR_INVALID_STATE) ?
	ERR_STILL_IN_CALL              = 0x6E04,
	// TODO(ppershing): move payload to ERR_INVALID_DATA
	// P1, P2 or payload is invalid
	ERR_INVALID_REQUEST_PARAMETERS = 0x6E05,
	// Request is not valid in the context of previous calls
	ERR_INVALID_STATE              = 0x6E06,
	// Some part of request data is invalid
	ERR_INVALID_DATA               = 0x6E07,

	// end of errors which trigger automatic response
	_ERR_AUTORESPOND_END           = 0x6E08,

	// Errors below SHOULD NOT be returned to the client
	// Instead, leaking these to the main() scope
	// means unexpected programming error
	// and we should stop further processing
	// to avoid exploits


	// Internal errors
	ERR_ASSERT                = 0x4700,

	// stream
	ERR_NOT_ENOUGH_INPUT      = 0x4710,
	ERR_DATA_TOO_LARGE        = 0x4711,

	// cbor
	ERR_UNEXPECTED_TOKEN      = 0x4720,

};

#endif
