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

// Validates a HS256 JWT (without "Bearer " prefix).
// Checks structure, signature (GRIDGUARD_JWT_SECRET), and expiry.
// Returns 0 and fills *claims on success, -1 on any failure.
int JWT_Validate(const char *token, JWTClaims *claims);

#endif // JWT_VALIDATOR_H
