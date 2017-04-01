#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <voikko.h>
#include "sqlite3ext.h"
#include "fts3_tokenizer.h"

SQLITE_EXTENSION_INIT1

#ifdef DEBUG
#define debug(...) {fprintf(stderr, "DEBUG :: "); fprintf(stderr, __VA_ARGS__);}
#else
#define debug(...)
#endif

#define SEARCH_PATH "/home/lewtds/dev/finndict/voikko-fts/dict"

struct voikko_tokenizer {
    sqlite3_tokenizer base;
    struct VoikkoHandle *voikko;
};

struct voikko_tokenizer_cursor {
    sqlite3_tokenizer_cursor base;
    struct voikko_tokenizer* tokenizer;

    char* pInput;
    char* pCrrToken;
    int bytesLeft;
    int crrTokenPosition;
    char* prevTokenBuf;
};

int tokenizer_create(
  int argc,                           /* Size of argv array */
  const char *const*argv,             /* Tokenizer argument strings */
  struct sqlite3_tokenizer **ppTokenizer     /* OUT: Created tokenizer */
);

int tokenizer_destroy(sqlite3_tokenizer *pTokenizer);

int tokenizer_open(
  sqlite3_tokenizer *pTokenizer,       /* Tokenizer object */
  const char *pInput, int nBytes,      /* Input buffer */
  sqlite3_tokenizer_cursor **ppCursor  /* OUT: Created tokenizer cursor */
);

int tokenizer_next_token(
  sqlite3_tokenizer_cursor *pCursor,   /* Tokenizer cursor */
  const char **ppToken, int *pnBytes,  /* OUT: Normalized text for token */
  int *piStartOffset,  /* OUT: Byte offset of token in input buffer */
  int *piEndOffset,    /* OUT: Byte offset of end of token in input buffer */
  int *piPosition      /* OUT: Number of tokens returned before this one */
);

int tokenizer_close(sqlite3_tokenizer_cursor *pCursor);


struct sqlite3_tokenizer_module noice = {
    .iVersion = 0,
    .xCreate = tokenizer_create,
    .xDestroy = tokenizer_destroy,
    .xOpen = tokenizer_open,
    .xClose = tokenizer_close,
    .xNext = tokenizer_next_token
};


/*
** Register a tokenizer implementation with FTS3 or FTS4.
*/
int registerTokenizer(
  sqlite3 *db,
  char *zName,
  const sqlite3_tokenizer_module *p
){
  int rc;
  sqlite3_stmt *pStmt;
  const char *zSql = "SELECT fts3_tokenizer(?, ?)";

  rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
  if( rc!=SQLITE_OK ){
    return rc;
  }

  sqlite3_bind_text(pStmt, 1, zName, -1, SQLITE_STATIC);
  sqlite3_bind_blob(pStmt, 2, &p, sizeof(p), SQLITE_STATIC);
  sqlite3_step(pStmt);

  return sqlite3_finalize(pStmt);
}


#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_voikkofts_init(
  sqlite3 *db,
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
){
  debug("Registering voikko tokenizer\n");
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  registerTokenizer(db, "voikko", &noice);
  return rc;
}


// SQLITE3 Module Implementation

int tokenizer_create(
  int argc,                           /* Size of argv array */
  const char *const*argv,             /* Tokenizer argument strings */
  sqlite3_tokenizer **ppTokenizer     /* OUT: Created tokenizer */
) {
    *ppTokenizer = calloc(1, sizeof(struct voikko_tokenizer));
    struct sqlite3_tokenizer *a = *ppTokenizer;
    a->pModule = &noice;

    struct voikko_tokenizer* tokzer = (struct voikko_tokenizer*) *ppTokenizer;
    const char *error;
    tokzer->voikko = voikkoInit(&error, "fi-morpho", SEARCH_PATH);

    if (error != NULL) {
        fprintf(stderr, "Initialization error: %s\n", error);
        return SQLITE_ERROR;
    }

    return SQLITE_OK;
}

int tokenizer_destroy(sqlite3_tokenizer *pTokenizer) {
    struct voikko_tokenizer* tok = (struct voikko_tokenizer*) pTokenizer;
    voikkoTerminate(tok->voikko);
    free(pTokenizer);
    return SQLITE_OK;
}

int tokenizer_open(
  sqlite3_tokenizer *pTokenizer,       /* Tokenizer object */
  const char *pInput, int nBytes,      /* Input buffer */
  sqlite3_tokenizer_cursor **ppCursor  /* OUT: Created tokenizer cursor */
) {
    debug("tokenizer_open()\n");
    *ppCursor = calloc(1, sizeof(struct voikko_tokenizer_cursor));

    struct voikko_tokenizer_cursor* cursor = (struct voikko_tokenizer_cursor*) *ppCursor;
    cursor->bytesLeft = nBytes;
    cursor->pInput = strndup(pInput, nBytes);
    cursor->pCrrToken = cursor->pInput;
    cursor->tokenizer = (struct voikko_tokenizer*) pTokenizer;

    return 0;
}

int tokenizer_close(sqlite3_tokenizer_cursor *pCursor) {
    debug("tokenizer_close()\n");
    struct voikko_tokenizer_cursor* cursor = (struct voikko_tokenizer_cursor*) pCursor;
    free(cursor->pInput);
    free(cursor);
}

int tokenizer_next_token(
  sqlite3_tokenizer_cursor *pCursor,   /* Tokenizer cursor */
  const char **ppToken, int *pnBytes,  /* OUT: Normalized text for token */
  int *piStartOffset,  /* OUT: Byte offset of token in input buffer */
  int *piEndOffset,    /* OUT: Byte offset of end of token in input buffer */
  int *piPosition      /* OUT: Number of tokens returned before this one */
) {
    debug("tokenizer_next_token()\n");
    struct voikko_tokenizer_cursor* cursor = (struct voikko_tokenizer_cursor*) pCursor;
    if (cursor->prevTokenBuf != NULL) {
        free(cursor->prevTokenBuf);
    }
    

    size_t tokenLen;
    enum voikko_token_type tokenType;

    do {
        printf("%p %d\n", cursor->pCrrToken, cursor->bytesLeft);
        tokenType = voikkoNextTokenCstr(
                   cursor->tokenizer->voikko,
                   cursor->pCrrToken,
                   cursor->bytesLeft, &tokenLen);

        *piStartOffset = cursor->pCrrToken - cursor->pInput;
        *piEndOffset = *piStartOffset + tokenLen;

        if (tokenType == TOKEN_NONE) {
            return SQLITE_DONE;
        }

        cursor->pCrrToken += tokenLen;
        cursor->bytesLeft -= tokenLen;
    } while (tokenType != TOKEN_WORD);

    char *orgToken = strndup(cursor->pCrrToken - tokenLen, tokenLen);
    struct voikko_mor_analysis **anas = voikkoAnalyzeWordCstr(cursor->tokenizer->voikko, orgToken);

    // voikkoAnalyzeWordCstr() actually returns a list of possible base forms but
    // we'll ignore it for now and use the first one. If there is none then return
    // the original token.
    if (anas != NULL && *anas != NULL) {
        struct voikko_mor_analysis *ana = anas[0];
        char *baseform = voikko_mor_analysis_value_cstr(ana, "BASEFORM");
        printf("%p\n", baseform);

        if (baseform != NULL) {
            *pnBytes = strlen(baseform);

            char *dup = strndup(baseform, *pnBytes);
            *ppToken = dup;
            cursor->prevTokenBuf = dup;
        }

        voikko_free_mor_analysis_value_cstr(baseform);
        free(orgToken);
    } else {
        *ppToken = orgToken;
        *pnBytes = tokenLen;
        cursor->prevTokenBuf = orgToken;
    }

    *piPosition = cursor->crrTokenPosition++;
    voikko_free_mor_analysis(anas);

    return SQLITE_OK;
}


int main() {
    const char* error;
    const char* text = "Olen vastuussa kolmannesta luokasta.";
    int textLen = strlen(text);

    struct VoikkoHandle *voi = voikkoInit(&error, "fi_FI-morpho", SEARCH_PATH);

    if (error) {
        fprintf(stderr, "Initialization error: %s\n", error);
        return -1;
    }

    enum voikko_token_type type;

    do {
        size_t tokenLen = 0;
        type = voikkoNextTokenCstr(voi, text, textLen, &tokenLen);

        if (type != TOKEN_NONE) {
            char *buf = strndup(text, tokenLen);

            printf("%d '%s'\n", type, buf);

            struct voikko_mor_analysis **anas = voikkoAnalyzeWordCstr(voi, buf);

            for (int k = 0; anas[k] != NULL; k++) {
                struct voikko_mor_analysis *ana = anas[k];

                const char **keys = voikko_mor_analysis_keys(ana);
                for (int i = 0; keys[i] != NULL; i++) {
                    char *value = voikko_mor_analysis_value_cstr(ana, keys[i]);
                    printf("%s: %s\n", keys[i], value);
                }

                printf("---------\n");
            }

            free(buf);

            text += tokenLen;
            textLen -= tokenLen;
        } else {
            break;
        }

    } while (type != TOKEN_NONE);


    return 0;
}
