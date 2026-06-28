/*
 * mini_fs.c - Simulador de Mini Sistema de Arquivos em Memoria
 *
 * Autores: Joao Arthur dos Santos Lorenzoni e Lucas Francelino
 * Disciplina: Sistemas Operacionais - UNIVALI
 * Professor: Michael D C Alves
 *
 * Compilar: gcc -o mini_fs mini_fs.c -Wall -Wextra
 * Executar: ./mini_fs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* ======================================================================
 * CONSTANTES
 * ====================================================================== */

#define BLOCK_SIZE           64          /* tamanho de cada bloco em bytes        */
#define NUM_BLOCKS           256         /* numero total de blocos no disco        */
#define DISK_SIZE            (BLOCK_SIZE * NUM_BLOCKS)  /* tamanho total do disco */
#define MAX_INODES           64          /* maximo de inodes/arquivos              */
#define MAX_NAME_LEN         64          /* tamanho maximo do nome                 */
#define MAX_CHILDREN         32          /* maximo de filhos por diretorio         */
#define MAX_BLOCKS_PER_FILE  16          /* maximo de blocos por arquivo           */
#define MAX_PATH_LEN         512         /* tamanho maximo de path                 */
#define MAX_CMD_LEN          2048        /* tamanho maximo de linha de comando     */
#define MAX_CONTENT_LEN      (MAX_BLOCKS_PER_FILE * BLOCK_SIZE)

/* Bits de permissao (estilo Unix) */
#define PERM_UR  0400   /* owner read    */
#define PERM_UW  0200   /* owner write   */
#define PERM_UX  0100   /* owner execute */
#define PERM_GR  0040   /* group read    */
#define PERM_GW  0020   /* group write   */
#define PERM_GX  0010   /* group execute */
#define PERM_OR  0004   /* other read    */
#define PERM_OW  0002   /* other write   */
#define PERM_OX  0001   /* other execute */

/* Papeis de usuario */
#define ROLE_OWNER  0
#define ROLE_GROUP  1
#define ROLE_OTHER  2

/* ======================================================================
 * ESTRUTURAS DE DADOS
 * ====================================================================== */

/*
 * Tipo de arquivo.
 * Representa se o conteudo e numerico, de caracteres, binario,
 * um programa executavel ou um diretorio.
 */
typedef enum {
    FT_NUMERIC   = 0,
    FT_CHAR      = 1,
    FT_BINARY    = 2,
    FT_PROGRAM   = 3,
    FT_DIRECTORY = 4
} FileType;

/*
 * File Control Block (FCB) / Inode.
 * Armazena todos os metadados de um arquivo ou diretorio,
 * simulando um inode Unix. Cada arquivo possui exatamente um FCB.
 * Os ponteiros de bloco (blocks[]) implementam alocacao indexada:
 * o FCB age como o indice que mapeia numero-logico-de-bloco -> bloco-fisico.
 */
typedef struct {
    int      id;                            /* identificador unico (inode number)  */
    int      active;                        /* 1 = em uso, 0 = livre               */
    char     name[MAX_NAME_LEN];            /* nome do arquivo ou diretorio        */
    FileType type;                          /* tipo de arquivo                     */
    int      size;                          /* tamanho em bytes                    */
    int      permissions;                   /* bitmask RWX (9 bits)                */
    int      owner_uid;                     /* uid do proprietario                 */
    int      group_gid;                     /* gid do grupo                        */
    time_t   created_at;                    /* data/hora de criacao                */
    time_t   modified_at;                   /* data/hora de modificacao            */
    time_t   accessed_at;                   /* data/hora de ultimo acesso          */
    int      block_count;                   /* quantidade de blocos alocados       */
    int      blocks[MAX_BLOCKS_PER_FILE];   /* indices dos blocos no disco simulado */
} Inode;

/*
 * No da arvore de diretorios.
 * Cada DirNode aponta para um Inode na tabela de inodes e mantem
 * referencias para o no pai e para os nos filhos (subdiretorios e arquivos).
 * A estrutura em arvore permite nomeacao hierarquica, agrupamento logico
 * e navegacao eficiente (O(profundidade) para resolucao de caminhos).
 */
typedef struct DirNode {
    int              inode_id;                   /* inode associado                */
    struct DirNode  *parent;                     /* diretorio pai                  */
    struct DirNode  *children[MAX_CHILDREN];     /* filhos (arquivos e subdir)      */
    int              child_count;                /* numero de filhos               */
} DirNode;

/* ======================================================================
 * VARIAVEIS GLOBAIS
 * ====================================================================== */

/* Disco simulado: array de caracteres que representa o armazenamento fisico */
static char    disk[DISK_SIZE];

/* Bitmap de blocos: 0 = livre, 1 = ocupado */
static int     block_bitmap[NUM_BLOCKS];

/* Tabela de inodes: repositorio central de metadados de todos os arquivos */
static Inode   inode_table[MAX_INODES];
static int     inode_count = 0;

/* Pool de nos de diretorio: evita malloc/free individual */
static DirNode node_pool[MAX_INODES];
static int     node_pool_count = 0;

/* Ponteiros para raiz e diretorio atual */
static DirNode *root_dir    = NULL;
static DirNode *current_dir = NULL;

/* Usuario atual: 0=owner, 1=group, 2=other */
static int current_uid = ROLE_OWNER;
static int current_gid = 0;

/* ======================================================================
 * FUNCOES AUXILIARES
 * ====================================================================== */

static const char *filetype_str(FileType t) {
    switch (t) {
        case FT_NUMERIC:   return "numeric";
        case FT_CHAR:      return "character";
        case FT_BINARY:    return "binary";
        case FT_PROGRAM:   return "program";
        case FT_DIRECTORY: return "directory";
        default:           return "unknown";
    }
}

static const char *role_str(int uid) {
    switch (uid) {
        case ROLE_OWNER: return "owner";
        case ROLE_GROUP: return "group";
        case ROLE_OTHER: return "other";
        default:         return "other";
    }
}

/* Formata permissoes como string "drwxrwxrwx" */
static void fmt_permissions(int perm, int is_dir, char *out) {
    out[0]  = is_dir         ? 'd' : '-';
    out[1]  = (perm & PERM_UR) ? 'r' : '-';
    out[2]  = (perm & PERM_UW) ? 'w' : '-';
    out[3]  = (perm & PERM_UX) ? 'x' : '-';
    out[4]  = (perm & PERM_GR) ? 'r' : '-';
    out[5]  = (perm & PERM_GW) ? 'w' : '-';
    out[6]  = (perm & PERM_GX) ? 'x' : '-';
    out[7]  = (perm & PERM_OR) ? 'r' : '-';
    out[8]  = (perm & PERM_OW) ? 'w' : '-';
    out[9]  = (perm & PERM_OX) ? 'x' : '-';
    out[10] = '\0';
}

/*
 * Verificacao de permissao (bloqueio obrigatorio).
 * Determina se o usuario atual possui as permissoes solicitadas
 * sobre um inode, conforme o modelo owner/group/other.
 * Retorna 1 se permitido, 0 se negado.
 */
static int check_permission(Inode *inode, int need_r, int need_w, int need_x) {
    int perm = inode->permissions;
    int r, w, x;

    if (current_uid == ROLE_OWNER && inode->owner_uid == ROLE_OWNER) {
        /* usuario e o proprietario */
        r = (perm & PERM_UR) != 0;
        w = (perm & PERM_UW) != 0;
        x = (perm & PERM_UX) != 0;
    } else if (current_uid == ROLE_GROUP && inode->group_gid == current_gid) {
        /* usuario pertence ao grupo */
        r = (perm & PERM_GR) != 0;
        w = (perm & PERM_GW) != 0;
        x = (perm & PERM_GX) != 0;
    } else {
        /* outros usuarios */
        r = (perm & PERM_OR) != 0;
        w = (perm & PERM_OW) != 0;
        x = (perm & PERM_OX) != 0;
    }

    if (need_r && !r) return 0;
    if (need_w && !w) return 0;
    if (need_x && !x) return 0;
    return 1;
}

/* Aloca um bloco livre no disco; retorna indice ou -1 se disco cheio */
static int alloc_block(void) {
    for (int i = 0; i < NUM_BLOCKS; i++) {
        if (!block_bitmap[i]) {
            block_bitmap[i] = 1;
            return i;
        }
    }
    return -1;
}

/* Libera um bloco e limpa seu conteudo */
static void free_block(int idx) {
    if (idx >= 0 && idx < NUM_BLOCKS) {
        block_bitmap[idx] = 0;
        memset(disk + idx * BLOCK_SIZE, 0, BLOCK_SIZE);
    }
}

/* Aloca um novo inode na tabela; retorna ponteiro ou NULL */
static Inode *alloc_inode(const char *name, FileType type, int perm) {
    if (inode_count >= MAX_INODES) return NULL;
    Inode *in = &inode_table[inode_count];
    memset(in, 0, sizeof(Inode));
    in->id          = inode_count++;
    in->active      = 1;
    strncpy(in->name, name, MAX_NAME_LEN - 1);
    in->type        = type;
    in->size        = 0;
    in->permissions = perm;
    in->owner_uid   = current_uid;
    in->group_gid   = current_gid;
    time_t now      = time(NULL);
    in->created_at  = now;
    in->modified_at = now;
    in->accessed_at = now;
    in->block_count = 0;
    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) in->blocks[i] = -1;
    return in;
}

/* Valida nome de arquivo/diretorio: nao pode ser vazio, muito longo ou conter '/' */
static int is_valid_name(const char *name) {
    if (!name || strlen(name) == 0 || strlen(name) >= MAX_NAME_LEN) return 0;
    if (strchr(name, '/') != NULL) return 0;
    return 1;
}

/* Retorna inode por ID ou NULL se inativo/invalido */
static Inode *get_inode(int id) {
    if (id < 0 || id >= inode_count) return NULL;
    if (!inode_table[id].active) return NULL;
    return &inode_table[id];
}

/* Aloca um DirNode do pool */
static DirNode *alloc_dir_node(int inode_id) {
    if (node_pool_count >= MAX_INODES) return NULL;
    DirNode *n = &node_pool[node_pool_count++];
    memset(n, 0, sizeof(DirNode));
    n->inode_id    = inode_id;
    n->parent      = NULL;
    n->child_count = 0;
    return n;
}

/* Busca filho por nome em um diretorio */
static DirNode *find_child(DirNode *dir, const char *name) {
    for (int i = 0; i < dir->child_count; i++) {
        Inode *in = get_inode(dir->children[i]->inode_id);
        if (in && strcmp(in->name, name) == 0)
            return dir->children[i];
    }
    return NULL;
}

/* Adiciona filho ao diretorio */
static int add_child(DirNode *dir, DirNode *child) {
    if (dir->child_count >= MAX_CHILDREN) return -1;
    dir->children[dir->child_count++] = child;
    child->parent = dir;
    return 0;
}

/* Remove filho do diretorio (substituicao pelo ultimo elemento) */
static void remove_child(DirNode *dir, DirNode *child) {
    for (int i = 0; i < dir->child_count; i++) {
        if (dir->children[i] == child) {
            dir->children[i] = dir->children[dir->child_count - 1];
            dir->child_count--;
            return;
        }
    }
}

/* Constroi caminho absoluto do diretorio atual */
static void get_pwd(char *out) {
    if (current_dir == root_dir) {
        strcpy(out, "/");
        return;
    }
    /* Coleta os nomes do no atual ate a raiz */
    const char *names[MAX_PATH_LEN / 2];
    int depth = 0;
    DirNode *n = current_dir;
    while (n && n != root_dir && depth < (MAX_PATH_LEN / 2 - 1)) {
        Inode *in = get_inode(n->inode_id);
        names[depth++] = in ? in->name : "?";
        n = n->parent;
    }
    /* Constroi o path de tras para frente */
    out[0] = '\0';
    for (int i = depth - 1; i >= 0; i--) {
        strncat(out, "/", MAX_PATH_LEN - strlen(out) - 1);
        strncat(out, names[i], MAX_PATH_LEN - strlen(out) - 1);
    }
}

/*
 * Escreve conteudo no arquivo usando alocacao indexada de blocos.
 * O conteudo e dividido em fatias de BLOCK_SIZE bytes; cada fatia
 * e gravada em um bloco fisico cujo indice fica registrado no array
 * blocks[] do inode (tabela de indices diretos).
 */
static int write_content(Inode *in, const char *content) {
    /* libera blocos anteriores */
    for (int i = 0; i < in->block_count; i++) {
        free_block(in->blocks[i]);
        in->blocks[i] = -1;
    }
    in->block_count = 0;
    in->size = 0;

    int len = (int)strlen(content);
    if (len == 0) return 0;

    int offset = 0;
    while (offset < len) {
        if (in->block_count >= MAX_BLOCKS_PER_FILE) {
            printf("Erro: arquivo muito grande (max %d bytes)\n", MAX_CONTENT_LEN);
            return -1;
        }
        int blk = alloc_block();
        if (blk < 0) {
            printf("Erro: disco cheio\n");
            return -1;
        }
        int to_write = len - offset;
        if (to_write > BLOCK_SIZE) to_write = BLOCK_SIZE;
        memcpy(disk + blk * BLOCK_SIZE, content + offset, to_write);
        in->blocks[in->block_count++] = blk;
        offset += to_write;
    }
    in->size        = len;
    in->modified_at = time(NULL);
    return 0;
}

/* Le conteudo dos blocos do inode para um buffer */
static void read_content(Inode *in, char *buf, int bufsize) {
    int offset = 0;
    for (int i = 0; i < in->block_count && offset < bufsize - 1; i++) {
        int blk = in->blocks[i];
        if (blk < 0) break;
        int to_read = BLOCK_SIZE;
        if (offset + to_read >= bufsize) to_read = bufsize - offset - 1;
        memcpy(buf + offset, disk + blk * BLOCK_SIZE, to_read);
        offset += to_read;
    }
    /* garante termino de string no tamanho real do arquivo */
    if (offset > in->size) offset = in->size;
    buf[offset]     = '\0';
    in->accessed_at = time(NULL);
}

/* ======================================================================
 * INICIALIZACAO DO SISTEMA DE ARQUIVOS
 * ====================================================================== */

static void fs_init(void) {
    memset(disk,         0, sizeof(disk));
    memset(block_bitmap, 0, sizeof(block_bitmap));
    memset(inode_table,  0, sizeof(inode_table));
    memset(node_pool,    0, sizeof(node_pool));
    inode_count     = 0;
    node_pool_count = 0;
    current_uid     = ROLE_OWNER;
    current_gid     = 0;

    /* Inode 0 e a raiz "/" com permissoes 0755 (rwxr-xr-x) */
    Inode *root_in = alloc_inode("/", FT_DIRECTORY, 0755);
    root_dir    = alloc_dir_node(root_in->id);
    current_dir = root_dir;
}

/* ======================================================================
 * IMPLEMENTACAO DOS COMANDOS
 * ====================================================================== */

/* ls [-l] : lista o diretorio atual */
static void cmd_ls(int long_fmt) {
    Inode *dir_in = get_inode(current_dir->inode_id);
    if (dir_in && !check_permission(dir_in, 1, 0, 0)) {
        printf("ls: permissao negada\n");
        return;
    }
    if (current_dir->child_count == 0) {
        printf("(diretorio vazio)\n");
        return;
    }
    for (int i = 0; i < current_dir->child_count; i++) {
        DirNode *child = current_dir->children[i];
        Inode *in = get_inode(child->inode_id);
        if (!in) continue;
        if (long_fmt) {
            char perm_str[12];
            fmt_permissions(in->permissions, in->type == FT_DIRECTORY, perm_str);
            char time_buf[32];
            struct tm *tm_info = localtime(&in->modified_at);
            strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", tm_info);
            printf("%s  inode:%-3d  %-6s  %6d  %s  %s\n",
                   perm_str, in->id,
                   role_str(in->owner_uid),
                   in->size,
                   time_buf,
                   in->name);
        } else {
            if (in->type == FT_DIRECTORY)
                printf("%s/  ", in->name);
            else
                printf("%s  ", in->name);
        }
    }
    if (!long_fmt) printf("\n");
}

/* pwd : mostra diretorio atual */
static void cmd_pwd(void) {
    char path[MAX_PATH_LEN];
    get_pwd(path);
    printf("%s\n", path);
}

/* mkdir <name> : cria diretorio */
static void cmd_mkdir(const char *name) {
    if (!is_valid_name(name)) {
        printf("mkdir: nome invalido (nao pode ser vazio, muito longo ou conter '/')\n");
        return;
    }
    Inode *dir_in = get_inode(current_dir->inode_id);
    if (dir_in && !check_permission(dir_in, 0, 1, 0)) {
        printf("mkdir: permissao negada\n");
        return;
    }
    if (find_child(current_dir, name)) {
        printf("mkdir: '%s': ja existe\n", name);
        return;
    }
    Inode *new_in = alloc_inode(name, FT_DIRECTORY, 0755);
    if (!new_in) { printf("mkdir: tabela de inodes cheia\n"); return; }
    DirNode *new_node = alloc_dir_node(new_in->id);
    if (!new_node) { printf("mkdir: pool de nos cheio\n"); return; }
    add_child(current_dir, new_node);
    printf("Diretorio '%s' criado (inode %d).\n", name, new_in->id);
}

/* cd <name|..|/> : navega entre diretorios */
static void cmd_cd(const char *name) {
    if (strcmp(name, "/") == 0) {
        current_dir = root_dir;
        return;
    }
    if (strcmp(name, "..") == 0) {
        if (current_dir->parent) current_dir = current_dir->parent;
        return;
    }
    if (strcmp(name, ".") == 0) return;

    DirNode *target = find_child(current_dir, name);
    if (!target) {
        printf("cd: '%s': diretorio nao encontrado\n", name);
        return;
    }
    Inode *in = get_inode(target->inode_id);
    if (!in || in->type != FT_DIRECTORY) {
        printf("cd: '%s': nao e um diretorio\n", name);
        return;
    }
    if (!check_permission(in, 0, 0, 1)) {
        printf("cd: permissao negada\n");
        return;
    }
    current_dir = target;
}

/* touch <name> [type] : cria arquivo vazio */
static void cmd_touch(const char *name, const char *type_str) {
    if (!is_valid_name(name)) {
        printf("touch: nome invalido (nao pode ser vazio, muito longo ou conter '/')\n");
        return;
    }
    Inode *dir_in = get_inode(current_dir->inode_id);
    if (dir_in && !check_permission(dir_in, 0, 1, 0)) {
        printf("touch: permissao negada\n");
        return;
    }
    /* Se ja existe, apenas atualiza timestamps */
    DirNode *existing = find_child(current_dir, name);
    if (existing) {
        Inode *in = get_inode(existing->inode_id);
        if (in) {
            time_t now       = time(NULL);
            in->accessed_at  = now;
            in->modified_at  = now;
        }
        printf("touch: timestamps de '%s' atualizados.\n", name);
        return;
    }
    FileType ft = FT_CHAR;
    if (type_str) {
        if      (strcmp(type_str, "numeric") == 0) ft = FT_NUMERIC;
        else if (strcmp(type_str, "binary")  == 0) ft = FT_BINARY;
        else if (strcmp(type_str, "program") == 0) ft = FT_PROGRAM;
        else                                        ft = FT_CHAR;
    }
    Inode *new_in = alloc_inode(name, ft, 0644);
    if (!new_in) { printf("touch: tabela de inodes cheia\n"); return; }
    DirNode *new_node = alloc_dir_node(new_in->id);
    if (!new_node) { printf("touch: pool de nos cheio\n"); return; }
    add_child(current_dir, new_node);
    printf("Arquivo '%s' criado (inode %d, tipo: %s, perms: 0644).\n",
           name, new_in->id, filetype_str(ft));
}

/* write <name> "<content>" : escreve conteudo no arquivo (equivalente a echo >) */
static void cmd_write(const char *name, const char *content) {
    DirNode *node = find_child(current_dir, name);
    if (!node) {
        printf("write: '%s': arquivo nao encontrado\n", name);
        return;
    }
    Inode *in = get_inode(node->inode_id);
    if (!in || in->type == FT_DIRECTORY) {
        printf("write: '%s': e um diretorio\n", name);
        return;
    }
    if (!check_permission(in, 0, 1, 0)) {
        printf("write: permissao negada\n");
        return;
    }
    if (write_content(in, content) == 0) {
        printf("Escrito %d bytes em '%s' distribuidos em %d bloco(s) de %d bytes.\n",
               in->size, name, in->block_count, BLOCK_SIZE);
    }
}

/* cat <name> : exibe conteudo do arquivo */
static void cmd_cat(const char *name) {
    DirNode *node = find_child(current_dir, name);
    if (!node) {
        printf("cat: '%s': arquivo nao encontrado\n", name);
        return;
    }
    Inode *in = get_inode(node->inode_id);
    if (!in || in->type == FT_DIRECTORY) {
        printf("cat: '%s': e um diretorio\n", name);
        return;
    }
    if (!check_permission(in, 1, 0, 0)) {
        printf("cat: permissao negada\n");
        return;
    }
    if (in->size == 0) {
        printf("(arquivo vazio)\n");
        return;
    }
    char buf[MAX_CONTENT_LEN + 1];
    read_content(in, buf, sizeof(buf));
    printf("%s\n", buf);
}

/* rm <name> : remove arquivo */
static void cmd_rm(const char *name) {
    DirNode *node = find_child(current_dir, name);
    if (!node) {
        printf("rm: '%s': arquivo nao encontrado\n", name);
        return;
    }
    Inode *in = get_inode(node->inode_id);
    if (!in) return;
    if (in->type == FT_DIRECTORY) {
        printf("rm: '%s': e um diretorio — use rmdir\n", name);
        return;
    }
    Inode *dir_in = get_inode(current_dir->inode_id);
    if (dir_in && !check_permission(dir_in, 0, 1, 0)) {
        printf("rm: permissao negada\n");
        return;
    }
    for (int i = 0; i < in->block_count; i++) free_block(in->blocks[i]);
    in->active = 0;
    remove_child(current_dir, node);
    printf("Arquivo '%s' removido.\n", name);
}

/* rmdir <name> : remove diretorio vazio */
static void cmd_rmdir(const char *name) {
    DirNode *node = find_child(current_dir, name);
    if (!node) {
        printf("rmdir: '%s': diretorio nao encontrado\n", name);
        return;
    }
    Inode *in = get_inode(node->inode_id);
    if (!in || in->type != FT_DIRECTORY) {
        printf("rmdir: '%s': nao e um diretorio\n", name);
        return;
    }
    if (node->child_count > 0) {
        printf("rmdir: '%s': diretorio nao esta vazio\n", name);
        return;
    }
    Inode *dir_in = get_inode(current_dir->inode_id);
    if (dir_in && !check_permission(dir_in, 0, 1, 0)) {
        printf("rmdir: permissao negada\n");
        return;
    }
    in->active = 0;
    remove_child(current_dir, node);
    printf("Diretorio '%s' removido.\n", name);
}

/* cp <src> <dst> : copia arquivo */
static void cmd_cp(const char *src, const char *dst) {
    DirNode *src_node = find_child(current_dir, src);
    if (!src_node) {
        printf("cp: '%s': arquivo nao encontrado\n", src);
        return;
    }
    Inode *src_in = get_inode(src_node->inode_id);
    if (!src_in || src_in->type == FT_DIRECTORY) {
        printf("cp: '%s': e um diretorio\n", src);
        return;
    }
    if (!check_permission(src_in, 1, 0, 0)) {
        printf("cp: permissao negada (origem)\n");
        return;
    }
    if (find_child(current_dir, dst)) {
        printf("cp: '%s': destino ja existe\n", dst);
        return;
    }
    Inode *dir_in = get_inode(current_dir->inode_id);
    if (dir_in && !check_permission(dir_in, 0, 1, 0)) {
        printf("cp: permissao negada (diretorio destino)\n");
        return;
    }
    Inode *dst_in = alloc_inode(dst, src_in->type, src_in->permissions);
    if (!dst_in) { printf("cp: tabela de inodes cheia\n"); return; }

    if (src_in->size > 0) {
        char buf[MAX_CONTENT_LEN + 1];
        read_content(src_in, buf, sizeof(buf));
        write_content(dst_in, buf);
    }
    DirNode *dst_node = alloc_dir_node(dst_in->id);
    if (!dst_node) { printf("cp: pool de nos cheio\n"); return; }
    add_child(current_dir, dst_node);
    printf("'%s' copiado para '%s' (inode %d).\n", src, dst, dst_in->id);
}

/* mv <src> <dst> : renomeia/move arquivo ou diretorio */
static void cmd_mv(const char *src, const char *dst) {
    DirNode *src_node = find_child(current_dir, src);
    if (!src_node) {
        printf("mv: '%s': nao encontrado\n", src);
        return;
    }
    if (find_child(current_dir, dst) && strcmp(src, dst) != 0) {
        printf("mv: '%s': destino ja existe\n", dst);
        return;
    }
    Inode *dir_in = get_inode(current_dir->inode_id);
    if (dir_in && !check_permission(dir_in, 0, 1, 0)) {
        printf("mv: permissao negada\n");
        return;
    }
    Inode *in = get_inode(src_node->inode_id);
    if (!in) return;
    strncpy(in->name, dst, MAX_NAME_LEN - 1);
    in->modified_at = time(NULL);
    printf("'%s' renomeado para '%s'.\n", src, dst);
}

/* chmod <octal> <name> : altera permissoes usando bitmask */
static void cmd_chmod(const char *octal_str, const char *name) {
    DirNode *node = find_child(current_dir, name);
    if (!node) {
        /* tenta tambem no diretorio atual (chmod .) */
        if (strcmp(name, ".") == 0) {
            node = current_dir;
        } else {
            printf("chmod: '%s': nao encontrado\n", name);
            return;
        }
    }
    Inode *in = get_inode(node->inode_id);
    if (!in) return;

    /* Somente o proprietario pode alterar permissoes */
    if (current_uid != ROLE_OWNER || in->owner_uid != ROLE_OWNER) {
        printf("chmod: operacao nao permitida (somente o proprietario pode alterar permissoes)\n");
        return;
    }
    /* Converte string octal para inteiro */
    int perm = 0;
    for (const char *p = octal_str; *p; p++) {
        if (*p < '0' || *p > '7') {
            printf("chmod: valor octal invalido '%s'\n", octal_str);
            return;
        }
        perm = perm * 8 + (*p - '0');
    }
    in->permissions = perm;
    char perm_str[12];
    fmt_permissions(perm, in->type == FT_DIRECTORY, perm_str);
    printf("Permissoes de '%s' alteradas para %04o (%s).\n", name, perm, perm_str);
}

/* stat <name|.> : exibe metadados completos do FCB/inode */
static void cmd_stat(const char *name) {
    Inode *in;
    if (strcmp(name, ".") == 0) {
        in = get_inode(current_dir->inode_id);
    } else {
        DirNode *node = find_child(current_dir, name);
        if (!node) {
            printf("stat: '%s': nao encontrado\n", name);
            return;
        }
        in = get_inode(node->inode_id);
    }
    if (!in) return;

    char perm_str[12];
    fmt_permissions(in->permissions, in->type == FT_DIRECTORY, perm_str);
    char ctime_buf[32], mtime_buf[32], atime_buf[32];
    strftime(ctime_buf, sizeof(ctime_buf), "%Y-%m-%d %H:%M:%S", localtime(&in->created_at));
    strftime(mtime_buf, sizeof(mtime_buf), "%Y-%m-%d %H:%M:%S", localtime(&in->modified_at));
    strftime(atime_buf, sizeof(atime_buf), "%Y-%m-%d %H:%M:%S", localtime(&in->accessed_at));

    printf("  Arquivo : %s\n",          in->name);
    printf("  Inode   : %d\n",          in->id);
    printf("  Tipo    : %s\n",          filetype_str(in->type));
    printf("  Tamanho : %d bytes\n",    in->size);
    printf("  Blocos  : %d alocados (%d bytes/bloco)\n", in->block_count, BLOCK_SIZE);
    if (in->block_count > 0) {
        printf("  Indices : ");
        for (int i = 0; i < in->block_count; i++)
            printf("[bloco %d @ offset %d]%s",
                   in->blocks[i], in->blocks[i] * BLOCK_SIZE,
                   i < in->block_count - 1 ? " -> " : "");
        printf("\n");
    }
    printf("  Permissoes: %s (%04o)\n", perm_str, in->permissions);
    printf("  Proprietario: %s\n",      role_str(in->owner_uid));
    printf("  Criado  : %s\n",          ctime_buf);
    printf("  Modificado: %s\n",        mtime_buf);
    printf("  Acessado: %s\n",          atime_buf);
}

/* whoami : mostra usuario atual */
static void cmd_whoami(void) {
    printf("Usuario atual: %s (uid=%d, gid=%d)\n",
           role_str(current_uid), current_uid, current_gid);
}

/* su <role> : troca de usuario simulado */
static void cmd_su(const char *role) {
    if      (strcmp(role, "owner") == 0) { current_uid = ROLE_OWNER; current_gid = 0; }
    else if (strcmp(role, "group") == 0) { current_uid = ROLE_GROUP; current_gid = 0; }
    else if (strcmp(role, "other") == 0) { current_uid = ROLE_OTHER; current_gid = 1; }
    else {
        printf("su: papel desconhecido '%s' (use: owner, group, other)\n", role);
        return;
    }
    printf("Trocado para usuario: %s\n", role_str(current_uid));
}

/* diskinfo : informacoes sobre uso do disco e inodes */
static void cmd_diskinfo(void) {
    int used_blocks = 0;
    for (int i = 0; i < NUM_BLOCKS; i++) if (block_bitmap[i]) used_blocks++;
    printf("Disco simulado:\n");
    printf("  Blocos totais : %d\n",   NUM_BLOCKS);
    printf("  Blocos usados : %d\n",   used_blocks);
    printf("  Blocos livres : %d\n",   NUM_BLOCKS - used_blocks);
    printf("  Tamanho/bloco : %d bytes\n", BLOCK_SIZE);
    printf("  Capacidade    : %d bytes (%d KB)\n", DISK_SIZE, DISK_SIZE / 1024);
    printf("  Usado         : %d bytes\n", used_blocks * BLOCK_SIZE);
    printf("Inodes: %d usados / %d total\n", inode_count, MAX_INODES);
}

/*
 * blocks <name> : demonstra alocacao indexada de blocos.
 * Mostra como o conteudo do arquivo foi dividido em blocos fisicos
 * e como o inode (FCB) referencia cada um desses blocos.
 */
static void cmd_blocks(const char *name) {
    DirNode *node = find_child(current_dir, name);
    if (!node) {
        printf("blocks: '%s': nao encontrado\n", name);
        return;
    }
    Inode *in = get_inode(node->inode_id);
    if (!in) return;
    if (in->type == FT_DIRECTORY) {
        printf("blocks: '%s': e um diretorio\n", name);
        return;
    }
    printf("Alocacao indexada de '%s' (inode %d):\n", name, in->id);
    if (in->block_count == 0) {
        printf("  Nenhum bloco alocado (arquivo vazio).\n");
        return;
    }
    printf("  Inode %d -> tabela de ponteiros de bloco:\n", in->id);
    for (int i = 0; i < in->block_count; i++) {
        int blk      = in->blocks[i];
        int start    = blk * BLOCK_SIZE;
        int end      = start + BLOCK_SIZE - 1;
        int bytes_in = (i == in->block_count - 1)
                       ? (in->size - i * BLOCK_SIZE)
                       : BLOCK_SIZE;
        char preview[17];
        int  plen = bytes_in < 16 ? bytes_in : 16;
        memcpy(preview, disk + blk * BLOCK_SIZE, plen);
        preview[plen] = '\0';
        /* remove caracteres nao-imprimiveis para exibicao */
        for (int k = 0; k < plen; k++)
            if (!isprint((unsigned char)preview[k])) preview[k] = '.';
        printf("  [%d] -> bloco fisico %3d | offset %5d-%5d | %d bytes | \"%s\"\n",
               i, blk, start, end, bytes_in, preview);
    }
    printf("  Total: %d bloco(s), %d bytes de dados, %d bytes alocados no disco.\n",
           in->block_count, in->size, in->block_count * BLOCK_SIZE);
}

/* tree : exibe arvore de diretorios recursivamente */
static void tree_recursive(DirNode *dir, int depth) {
    for (int i = 0; i < dir->child_count; i++) {
        DirNode *child = dir->children[i];
        Inode *in = get_inode(child->inode_id);
        if (!in) continue;
        for (int d = 0; d < depth; d++) printf("│   ");
        printf("├── %s%s\n", in->name, in->type == FT_DIRECTORY ? "/" : "");
        if (in->type == FT_DIRECTORY)
            tree_recursive(child, depth + 1);
    }
}

static void cmd_tree(void) {
    char path[MAX_PATH_LEN];
    get_pwd(path);
    printf("%s\n", path);
    tree_recursive(current_dir, 0);
}

/* help : lista todos os comandos disponiveis */
static void cmd_help(void) {
    printf("Mini Sistema de Arquivos - Comandos disponiveis:\n");
    printf("----------------------------------------------------\n");
    printf("  ls [-l]                  Lista o diretorio atual\n");
    printf("  pwd                      Mostra o diretorio atual\n");
    printf("  tree                     Exibe arvore de diretorios\n");
    printf("  mkdir <nome>             Cria diretorio\n");
    printf("  cd <nome|..|/>           Navega entre diretorios\n");
    printf("  touch <nome> [tipo]      Cria arquivo vazio\n");
    printf("                           tipos: numeric, char, binary, program\n");
    printf("  write <nome> \"<texto>\"  Escreve conteudo no arquivo\n");
    printf("  cat <nome>               Le e exibe conteudo do arquivo\n");
    printf("  cp <origem> <destino>    Copia arquivo\n");
    printf("  mv <origem> <destino>    Renomeia/move arquivo ou diretorio\n");
    printf("  rm <nome>                Remove arquivo\n");
    printf("  rmdir <nome>             Remove diretorio vazio\n");
    printf("  chmod <octal> <nome>     Altera permissoes (ex: 755, 644, 000)\n");
    printf("  stat <nome|.>            Exibe metadados do FCB/inode\n");
    printf("  blocks <nome>            Mostra alocacao de blocos do arquivo\n");
    printf("  diskinfo                 Informacoes de uso do disco\n");
    printf("  whoami                   Mostra usuario atual\n");
    printf("  su <owner|group|other>   Troca de usuario simulado\n");
    printf("  help                     Exibe esta ajuda\n");
    printf("  exit                     Encerra o simulador\n");
}

/* ======================================================================
 * PARSER DE COMANDOS
 * ====================================================================== */

/*
 * Tokeniza linha de comando respeitando strings entre aspas duplas.
 * Exemplo: write arq "ola mundo" -> ["write", "arq", "ola mundo"]
 */
static int tokenize(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p  = line;
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    return argc;
}

static void run_command(char *line) {
    line[strcspn(line, "\n\r")] = '\0';
    if (strlen(line) == 0) return;

    char *argv[16];
    int   argc = tokenize(line, argv, 16);
    if (argc == 0) return;

    const char *cmd = argv[0];

    if      (strcmp(cmd, "ls")      == 0) cmd_ls(argc > 1 && strcmp(argv[1], "-l") == 0);
    else if (strcmp(cmd, "pwd")     == 0) cmd_pwd();
    else if (strcmp(cmd, "tree")    == 0) cmd_tree();
    else if (strcmp(cmd, "mkdir")   == 0) {
        if (argc < 2) { printf("Uso: mkdir <nome>\n"); return; }
        cmd_mkdir(argv[1]);
    }
    else if (strcmp(cmd, "cd")      == 0) {
        if (argc < 2) { printf("Uso: cd <nome>\n"); return; }
        cmd_cd(argv[1]);
    }
    else if (strcmp(cmd, "touch")   == 0) {
        if (argc < 2) { printf("Uso: touch <nome> [tipo]\n"); return; }
        cmd_touch(argv[1], argc > 2 ? argv[2] : NULL);
    }
    else if (strcmp(cmd, "write")   == 0) {
        if (argc < 3) { printf("Uso: write <nome> \"<conteudo>\"\n"); return; }
        cmd_write(argv[1], argv[2]);
    }
    else if (strcmp(cmd, "cat")     == 0) {
        if (argc < 2) { printf("Uso: cat <nome>\n"); return; }
        cmd_cat(argv[1]);
    }
    else if (strcmp(cmd, "rm")      == 0) {
        if (argc < 2) { printf("Uso: rm <nome>\n"); return; }
        cmd_rm(argv[1]);
    }
    else if (strcmp(cmd, "rmdir")   == 0) {
        if (argc < 2) { printf("Uso: rmdir <nome>\n"); return; }
        cmd_rmdir(argv[1]);
    }
    else if (strcmp(cmd, "cp")      == 0) {
        if (argc < 3) { printf("Uso: cp <origem> <destino>\n"); return; }
        cmd_cp(argv[1], argv[2]);
    }
    else if (strcmp(cmd, "mv")      == 0) {
        if (argc < 3) { printf("Uso: mv <origem> <destino>\n"); return; }
        cmd_mv(argv[1], argv[2]);
    }
    else if (strcmp(cmd, "chmod")   == 0) {
        if (argc < 3) { printf("Uso: chmod <octal> <nome>\n"); return; }
        cmd_chmod(argv[1], argv[2]);
    }
    else if (strcmp(cmd, "stat")    == 0) {
        if (argc < 2) { printf("Uso: stat <nome|.>\n"); return; }
        cmd_stat(argv[1]);
    }
    else if (strcmp(cmd, "blocks")  == 0) {
        if (argc < 2) { printf("Uso: blocks <nome>\n"); return; }
        cmd_blocks(argv[1]);
    }
    else if (strcmp(cmd, "diskinfo")== 0) cmd_diskinfo();
    else if (strcmp(cmd, "whoami")  == 0) cmd_whoami();
    else if (strcmp(cmd, "su")      == 0) {
        if (argc < 2) { printf("Uso: su <owner|group|other>\n"); return; }
        cmd_su(argv[1]);
    }
    else if (strcmp(cmd, "help")    == 0) cmd_help();
    else if (strcmp(cmd, "exit")    == 0 || strcmp(cmd, "quit") == 0) {
        printf("Encerrando o simulador de sistema de arquivos.\n");
        exit(0);
    }
    else {
        printf("Comando desconhecido: '%s'. Digite 'help' para ver os comandos disponiveis.\n", cmd);
    }
}

/* ======================================================================
 * MAIN
 * ====================================================================== */

int main(void) {
    fs_init();

    printf("========================================================\n");
    printf("  Mini Sistema de Arquivos em Memoria\n");
    printf("  Autores: Joao Arthur dos Santos Lorenzoni\n");
    printf("           Lucas Francelino\n");
    printf("  Disciplina: Sistemas Operacionais - UNIVALI\n");
    printf("  Professor: Michael D C Alves\n");
    printf("========================================================\n");
    printf("Disco simulado: %d blocos x %d bytes = %d bytes totais\n",
           NUM_BLOCKS, BLOCK_SIZE, DISK_SIZE);
    printf("Digite 'help' para ver os comandos disponiveis.\n\n");

    char line[MAX_CMD_LEN];
    while (1) {
        char path[MAX_PATH_LEN];
        get_pwd(path);
        printf("[%s@miniFS %s]$ ", role_str(current_uid), path);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        run_command(line);
    }
    return 0;
}
