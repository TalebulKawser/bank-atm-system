#ifndef STATUS_H
#define STATUS_H

/*
 * Shared status codes for every module.
 * All public module functions return one of these values.
 * 0 means success; negative values indicate a specific failure.
 */
typedef enum
{
    OK                     =  0,
    ERR_INVALID_INPUT      = -1,
    ERR_NOT_FOUND          = -2,
    ERR_AUTH_FAILED        = -3,
    ERR_DUPLICATE_TYPE     = -4,
    ERR_ACCOUNT_INACTIVE   = -5,
    ERR_INSUFFICIENT_FUNDS = -6,
    ERR_LINK_DOWN          = -7,
    ERR_DB                 = -8,
    ERR_MEMORY             = -9,

    /* Additional shared transaction-specific failures. */
    ERR_INVALID_AMOUNT     = -10,
    ERR_INVALID_ACCOUNT    = -11,
    ERR_WRONG_ACCOUNT_TYPE = -12
} StatusCode;

#endif
