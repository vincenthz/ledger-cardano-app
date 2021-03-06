# Get Extended Public Key

**Description**

Get extended public key (i.e., public key + chain code) for a given BIP32 path.

Note: Unlike BTC app, this call does not return nor display addresses. See [](ins_derive_address.md) for details.

❓(IOHK):Check if the restriction makes sense.
This call restricts BIP32 path to start with `44'/1815'/account'`, i.e. it only returns public key for accounts and descendants. Currently there is no reason for a client to ask for any other path prefix. 

❓(IOHK):Check if the restriction makes sense.
This call restricts path length to be 10. Length 10 should be enough for all known application purposes.

**Command**

| Field | Value    |
| ----- | -------- |
| CLA   | `0xD7`   |
| INS   | `0x10`   |
| P1    | unused   |
| P2    | unused   |
| Lc    | variable |

**Data**

| Field                             | Length | Comments                  |
| --------------------------------- | ------ | ------------------------- |
| BIP32 path len                    | 1      | min 2, max 10             |
| First derivation index            | 4      | Big endian. Must be 44'   |
| Second derivation index           | 4      | Big endian. Must be 1815' |
| (optional) Third derivation index | 4      | Big endian                |
| ...                               | ...    | ...                       |
| (optional) Last derivation index  | 4      | Big endian                |

**Response**

| Field      | Length |
| ---------- | ------ |
| pub_key    | 32     |
| chain_code | 32     |

Concatenation of `pub_key` and `chain_code` represents extended public key.

**Errors (SW codes)**

- 0x9000 OK
- ❓(VL): describe error codes

**Ledger responsibilities**

- Check:
  - check P1 is valid
    - `P1 == 0`
  - check P2 is valid
    - `P2 == 0`
  - check data is valid:
    - `Lc >= 1` (we have path_len)
    - `1 + path_len * 4 == Lc`
  - check derivatoin path is valid and within Cardano BIP32 space
    - `path_len >= 3`
    - `path_len <= 10`
    - `path[0] == 44'` (' means hardened)
    - `path[1] == 1815'`
    - `path[2] is hardened` (`path[2]` is account number)
- calculate public key
- respond with public key
 
**TODOs**
- ❓(IOHK): Should we also support BTC app like token validation? (Note: Token validation is to prevent concurrent access to the Ledger by two different host apps which could confuse user into performing wrong actions)
- ❓(IOHK): Should we support permanent app setting where Ledger forces user to acknowledge public key retrieval before sending it to host? (Note: probably not in the first version of the app)
- ❓(IOHK): Should there be an option to show the public key on display? Is it useful in any way? (Note: probably not)

