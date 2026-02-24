#ifndef JWT_VALIDATOR_H
#define JWT_VALIDATOR_H

// Maximum length for the subject (user_id) extracted from JWT.
#define JWT_SUBJECT_MAX 128

// Parsed, verified JWT claims.
typedef struct
{
    char subject[JWT_SUBJECT_MAX]; // "sub" field — user identifier
    long expiresAt;                // "exp" field — Unix timestamp
} JWTClaims;

// Validate a JWT token string (without "Bearer " prefix).

// Verifies:
//   - Structure: three base64url-encoded parts separated by '.'
//   - Algorithm: HS256 (HMAC-SHA256)
//   - Signature: recomputed using GRIDGUARD_JWT_SECRET env var
//   - Expiry:    exp > current time
//

// On success: populates *claims and returns 0.
// On failure: returns -1 (logs reason internally).
int JWT_Validate(const char *token, JWTClaims *claims);

#endif // JWT_VALIDATOR_H
