#ifndef JWT_ISSUER_H
#define JWT_ISSUER_H

#include <time.h>

#define JWT_TOKEN_MAX 512

// Create JWT access token with userId (sub), email, and plan_type claims.
// Token expires in 24 hours for demo purposes.
// Returns  0 = ok,  -1 = error.
int JWTIssuer_CreateToken(const char *userId, const char *email, const char *planType, char *tokenOut);

#endif // JWT_ISSUER_H
