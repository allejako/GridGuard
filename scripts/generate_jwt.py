#!/usr/bin/env python3
"""Generate JWT token from platform.db using GridGuard's JWTIssuer.
Demonstrates that authentication comes from platform DB, not client DB.
"""

import sqlite3
import sys
import subprocess
import tempfile
import os

def get_user_from_platform(platform_db, user_id):
    """Fetch user from platform DB"""
    con = sqlite3.connect(platform_db)
    cur = con.execute(
        "SELECT user_id, email, plan_type FROM users WHERE user_id = ?",
        (user_id,)
    )
    row = cur.fetchone()
    con.close()

    if not row:
        print(f"Error: User '{user_id}' not found in platform DB", file=sys.stderr)
        sys.exit(1)

    return {
        'user_id': row[0],
        'email': row[1],
        'plan_type': row[2]
    }

def generate_jwt_using_c(user):
    """Call GridGuard's JWTIssuer via small C wrapper"""

    # Write a minimal C program that uses JWTIssuer
    c_code = f'''
#define _POSIX_C_SOURCE 200809L
#include "auth/JWTIssuer.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {{
    char token[JWT_TOKEN_MAX];

    // Save original stdout
    int saved_stdout = dup(STDOUT_FILENO);

    // Redirect stdout to /dev/null to suppress Logger output
    FILE *devnull = fopen("/dev/null", "w");
    dup2(fileno(devnull), STDOUT_FILENO);

    // Generate JWT (Logger output goes to /dev/null)
    int result = JWTIssuer_CreateToken("{user['user_id']}", "{user['email']}", "{user['plan_type']}", token);

    // Restore original stdout
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    fclose(devnull);

    if (result != 0) {{
        return 1;
    }}

    // Print only the token to restored stdout
    printf("%s", token);
    return 0;
}}
'''

    # Compile and run
    with tempfile.TemporaryDirectory() as tmpdir:
        c_file = os.path.join(tmpdir, "gen.c")
        bin_file = os.path.join(tmpdir, "gen")

        with open(c_file, 'w') as f:
            f.write(c_code)

        # Compile (use same flags as GridGuard)
        compile_cmd = [
            'gcc', '-std=c11', '-I', 'src',
            c_file,
            'build/auth/JWTIssuer.o',
            'build/sys/Logger.o',
            '-lmbedtls', '-lmbedx509', '-lmbedcrypto',
            '-o', bin_file
        ]

        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Compilation failed:\\n{result.stderr}", file=sys.stderr)
            sys.exit(1)

        # Run
        result = subprocess.run([bin_file], capture_output=True, text=True)
        if result.returncode != 0:
            print(f"JWT generation failed:\\n{result.stderr}", file=sys.stderr)
            sys.exit(1)

        return result.stdout.strip()

if __name__ == "__main__":
    platform_db = sys.argv[1] if len(sys.argv) > 1 else "platform.db"
    user_id = sys.argv[2] if len(sys.argv) > 2 else "test_user"

    # Ensure GridGuard is built
    if not os.path.exists("build/auth/JWTIssuer.o"):
        print("Error: GridGuard not built. Run 'make' first.", file=sys.stderr)
        sys.exit(1)

    # Fetch user from platform DB
    user = get_user_from_platform(platform_db, user_id)

    # Generate JWT using GridGuard's actual JWTIssuer
    token = generate_jwt_using_c(user)

    print(token)
