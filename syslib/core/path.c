/******************************************************************************
 * XASMOS - SuperDirs Path Parser (Delimitador Nativo '|')
 ******************************************************************************/
typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;

#define MAX_PATH_LENGTH  256
#define MAX_TOKENS       8
#define MAX_TOKEN_SIZE   32

typedef struct {
    char tokens[MAX_TOKENS][MAX_TOKEN_SIZE];
    uint32_t token_count;
} SuperDirPath;

/**
 * Procesa una ruta nativa de XASMOS utilizando '|' como delimitador.
 * Formato esperado: |directory|sub|file
 * Retorna 0 si es exitoso, -1 si viola las reglas de sintaxis.
 */
int parse_superdir_path(const char *raw_path, SuperDirPath *out_parsed) {
    if (raw_path[0] != '|') return -1; // Toda ruta debe iniciar obligatoriamente con '|'

    out_parsed->token_count = 0;
    uint32_t t_idx = 0;
    uint32_t c_idx = 0;

    // Recorrer la cadena de texto ignorando la herencia de UNIX
    for (uint32_t i = 1; raw_path[i] != '\0' && i < MAX_PATH_LENGTH; i++) {
        if (raw_path[i] == '|') {
            if (c_idx > 0) {
                out_parsed->tokens[t_idx][c_idx] = '\0'; // Cerrar el token actual
                t_idx++;
                c_idx = 0;
                if (t_idx >= MAX_TOKENS) break;
            }
        } else {
            if (c_idx < (MAX_TOKEN_SIZE - 1)) {
                out_parsed->tokens[t_idx][c_idx] = raw_path[i];
                c_idx++;
            }
        }
    }

    // Guardar el último token si la ruta no termina en '|'
    if (c_idx > 0 && t_idx < MAX_TOKENS) {
        out_parsed->tokens[t_idx][c_idx] = '\0';
        t_idx++;
    }

    out_parsed->token_count = t_idx;
    return (t_idx > 0) ? 0 : -1;
}
