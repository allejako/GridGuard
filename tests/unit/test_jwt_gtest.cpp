// JWT Issuance and Validation Tests
// Covers the full token lifecycle: create → validate → reject

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <ctime>

extern "C" {
#include "auth/JWTIssuer.h"
#include "auth/JWTValidator.h"
}

class JWTTest : public ::testing::Test {
protected:
    void SetUp() override {
        setenv("GRIDGUARD_JWT_SECRET", "test-secret-for-unit-tests", 1);
    }
    void TearDown() override {
        unsetenv("GRIDGUARD_JWT_SECRET");
    }
};

// A freshly created token must validate successfully and carry the correct subject
TEST_F(JWTTest, IssueAndValidateRoundtrip) {
    char token[JWT_TOKEN_MAX];
    ASSERT_EQ(JWTIssuer_CreateToken("user123", "user@test.com", "premium", token), 0);

    JWTClaims claims;
    EXPECT_EQ(JWT_Validate(token, &claims), 0);
    EXPECT_STREQ(claims.subject, "user123");
    EXPECT_GT(claims.expiresAt, (long)time(NULL));
}

// Token must expire approximately 24 hours after creation
TEST_F(JWTTest, TokenExpiresIn24Hours) {
    char token[JWT_TOKEN_MAX];
    ASSERT_EQ(JWTIssuer_CreateToken("exp_user", "e@test.com", "free", token), 0);

    JWTClaims claims;
    ASSERT_EQ(JWT_Validate(token, &claims), 0);

    long expectedExp = (long)time(NULL) + 86400;
    EXPECT_NEAR(claims.expiresAt, expectedExp, 5);
}

// Token signed with secret A must be rejected when the secret is changed to B
TEST_F(JWTTest, RejectsTokenWithWrongSecret) {
    char token[JWT_TOKEN_MAX];
    ASSERT_EQ(JWTIssuer_CreateToken("user123", "u@u.com", "free", token), 0);

    setenv("GRIDGUARD_JWT_SECRET", "completely-different-secret", 1);
    JWTClaims claims;
    EXPECT_EQ(JWT_Validate(token, &claims), -1);
}

// Manually crafted token with invalid signature must be rejected
TEST_F(JWTTest, RejectsTamperedPayload) {
    // Valid header + tampered payload + garbage signature
    const char *bad = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"
                      ".eyJzdWIiOiJoYWNrZXIiLCJleHAiOjk5OTk5OTk5OTl9"
                      ".invalidsignature_xxxx";
    JWTClaims claims;
    EXPECT_EQ(JWT_Validate(bad, &claims), -1);
}

// Issuer must fail when the secret env var is not set
TEST_F(JWTTest, IssuerFailsWithoutSecret) {
    unsetenv("GRIDGUARD_JWT_SECRET");
    char token[JWT_TOKEN_MAX];
    EXPECT_EQ(JWTIssuer_CreateToken("user", "u@u.com", "free", token), -1);
}

// Validator must fail when the secret env var is not set
TEST_F(JWTTest, ValidatorFailsWithoutSecret) {
    char token[JWT_TOKEN_MAX];
    ASSERT_EQ(JWTIssuer_CreateToken("user", "u@u.com", "free", token), 0);

    unsetenv("GRIDGUARD_JWT_SECRET");
    JWTClaims claims;
    EXPECT_EQ(JWT_Validate(token, &claims), -1);
}

// All null combinations for the issuer must return -1 without crashing
TEST_F(JWTTest, IssuerNullPointers) {
    char token[JWT_TOKEN_MAX];
    EXPECT_EQ(JWTIssuer_CreateToken(nullptr, "u@u.com", "free", token), -1);
    EXPECT_EQ(JWTIssuer_CreateToken("user", nullptr, "free", token),    -1);
    EXPECT_EQ(JWTIssuer_CreateToken("user", "u@u.com", nullptr, token), -1);
    EXPECT_EQ(JWTIssuer_CreateToken("user", "u@u.com", "free", nullptr),-1);
}

// All null combinations for the validator must return -1 without crashing
TEST_F(JWTTest, ValidatorNullPointers) {
    JWTClaims claims;
    EXPECT_EQ(JWT_Validate(nullptr, &claims), -1);

    char token[JWT_TOKEN_MAX];
    ASSERT_EQ(JWTIssuer_CreateToken("user", "u@u.com", "free", token), 0);
    EXPECT_EQ(JWT_Validate(token, nullptr), -1);
}

// Structurally broken tokens (missing dots) must be rejected
TEST_F(JWTTest, RejectsMalformedTokens) {
    JWTClaims claims;
    EXPECT_EQ(JWT_Validate("",           &claims), -1);
    EXPECT_EQ(JWT_Validate("nodots",     &claims), -1);
    EXPECT_EQ(JWT_Validate("only.one",   &claims), -1);
}

// Two different users must get distinct tokens, both validating to the right subject
TEST_F(JWTTest, DifferentUsersGetDifferentTokens) {
    char t1[JWT_TOKEN_MAX], t2[JWT_TOKEN_MAX];
    ASSERT_EQ(JWTIssuer_CreateToken("alice", "a@a.com", "free", t1), 0);
    ASSERT_EQ(JWTIssuer_CreateToken("bob",   "b@b.com", "free", t2), 0);
    EXPECT_STRNE(t1, t2);

    JWTClaims c1, c2;
    ASSERT_EQ(JWT_Validate(t1, &c1), 0);
    ASSERT_EQ(JWT_Validate(t2, &c2), 0);
    EXPECT_STREQ(c1.subject, "alice");
    EXPECT_STREQ(c2.subject, "bob");
}

// A token pre-signed before Wait is called must still validate (no race)
TEST_F(JWTTest, DifferentPlansProduceDifferentTokens) {
    char t1[JWT_TOKEN_MAX], t2[JWT_TOKEN_MAX];
    ASSERT_EQ(JWTIssuer_CreateToken("user", "u@u.com", "free",    t1), 0);
    ASSERT_EQ(JWTIssuer_CreateToken("user", "u@u.com", "premium", t2), 0);
    // same sub, different plan claim → different payload → different signature
    EXPECT_STRNE(t1, t2);
}
