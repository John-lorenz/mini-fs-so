# Mini Sistema de Arquivos em Memória

**Autores:** João Arthur dos Santos Lorenzoni e Lucas Francelino  
**Disciplina:** Sistemas Operacionais — UNIVALI  
**Professor:** Michael D C Alves

---

## Como Compilar e Executar

### Pré-requisito
GCC instalado em ambiente Linux, WSL ou macOS.

```bash
# Compilar
gcc -Wall -Wextra -std=c99 -o mini_fs mini_fs.c

# Ou usando o Makefile
make

# Executar
./mini_fs
```

### Exemplo de sessão completa

```
[owner@miniFS /]$ mkdir documentos
[owner@miniFS /]$ cd documentos
[owner@miniFS /documentos]$ touch relatorio.txt
[owner@miniFS /documentos]$ write relatorio.txt "Conteúdo do relatório de SO"
[owner@miniFS /documentos]$ cat relatorio.txt
[owner@miniFS /documentos]$ stat relatorio.txt
[owner@miniFS /documentos]$ blocks relatorio.txt
[owner@miniFS /documentos]$ chmod 640 relatorio.txt
[owner@miniFS /documentos]$ su other
[owner@miniFS /documentos]$ cat relatorio.txt   # deve negar
[owner@miniFS /documentos]$ su owner
[owner@miniFS /documentos]$ cp relatorio.txt copia.txt
[owner@miniFS /documentos]$ mv copia.txt backup.txt
[owner@miniFS /documentos]$ ls -l
[owner@miniFS /documentos]$ rm backup.txt
[owner@miniFS /documentos]$ diskinfo
[owner@miniFS /documentos]$ exit
```

---

## Estrutura do Código

O projeto é composto por um único arquivo fonte (`mini_fs.c`) organizado nas seguintes seções:

| Seção | Descrição |
|---|---|
| Constantes | Parâmetros do disco e limites do sistema |
| Estruturas de Dados | `Inode` (FCB) e `DirNode` (árvore de diretórios) |
| Variáveis Globais | Disco simulado, bitmap de blocos, tabela de inodes |
| Funções Auxiliares | Alocação, permissão, leitura/escrita em blocos |
| Inicialização | `fs_init()` — monta o sistema de arquivos na memória |
| Implementação dos Comandos | Uma função por comando (`cmd_ls`, `cmd_mkdir`, …) |
| Parser de Comandos | `tokenize()` e `run_command()` |
| `main()` | REPL — loop de leitura e execução de comandos |

---

## Escolhas de Design e Estruturas de Dados

### 1. File Control Block (FCB) / Inode

Representado pela struct `Inode` com os seguintes campos:

```c
typedef struct {
    int      id;                          // inode number único
    int      active;                      // 1 = em uso, 0 = liberado
    char     name[MAX_NAME_LEN];          // nome do arquivo
    FileType type;                        // numeric/char/binary/program/directory
    int      size;                        // tamanho em bytes
    int      permissions;                 // bitmask RWX de 9 bits (octal Unix)
    int      owner_uid;                   // uid do proprietário
    int      group_gid;                   // gid do grupo
    time_t   created_at;                  // timestamp de criação
    time_t   modified_at;                 // timestamp de modificação
    time_t   accessed_at;                 // timestamp de último acesso
    int      block_count;                 // número de blocos alocados
    int      blocks[MAX_BLOCKS_PER_FILE]; // ponteiros de bloco (alocação indexada)
} Inode;
```

Cada arquivo/diretório possui exatamente um `Inode` na tabela global `inode_table[]`, acessível diretamente pelo seu `id`. Isso simula a tabela de inodes do Unix.

### 2. Árvore de Diretórios (`DirNode`)

```c
typedef struct DirNode {
    int             inode_id;              // referência ao inode correspondente
    struct DirNode *parent;                // ponteiro para o diretório pai
    struct DirNode *children[MAX_CHILDREN]; // filhos (arquivos e subdiretórios)
    int             child_count;           // número de filhos
} DirNode;
```

A estrutura em **árvore** oferece:
- **Nomeação hierárquica**: `/documentos/relatorio.txt` — cada nível é resolvido percorrendo os filhos.
- **Agrupamento lógico**: arquivos relacionados ficam em um mesmo nó pai.
- **Eficiência**: busca em O(filhos_por_diretório), navegação (`cd`) em O(profundidade).
- **Integridade referencial**: o `parent` permite `cd ..` e construção de caminhos absolutos (`pwd`).

### 3. Disco Simulado e Alocação de Blocos

```c
static char disk[DISK_SIZE];       // array de 16 KB representa o "disco"
static int  block_bitmap[NUM_BLOCKS]; // 0=livre, 1=usado
```

O disco é dividido em **256 blocos de 64 bytes**. A alocação é **indexada**:
- O `Inode.blocks[]` age como um array de ponteiros diretos de blocos.
- Ao escrever, o conteúdo é fatiado em pedaços de `BLOCK_SIZE` bytes.
- Cada pedaço é gravado no primeiro bloco livre (encontrado via bitmap).
- O índice do bloco físico é armazenado em `blocks[i]`.
- O comando `blocks <nome>` exibe visualmente esse mapeamento.

**Exemplo** — arquivo de 100 bytes em blocos de 64:

```
Inode 3 -> tabela de ponteiros de bloco:
  [0] -> bloco físico  12 | offset  768-831 | 64 bytes | "Conteúdo do re"
  [1] -> bloco físico  13 | offset  832-895 | 36 bytes | "latório de SO"
```

### 4. Controle de Acesso e Permissões (RWX)

As permissões usam os mesmos **9 bits** do Unix (estilo octal):

| Bits | Owner | Group | Others |
|---|---|---|---|
| Read    | `0400` | `0040` | `0004` |
| Write   | `0200` | `0020` | `0002` |
| Execute | `0100` | `0010` | `0001` |

A verificação em `check_permission()` determina o conjunto de bits a aplicar com base no papel do `current_uid` (owner/group/other) em relação ao `owner_uid` do arquivo. Antes de cada operação de leitura, escrita ou execução, a função é chamada e, em caso de negação, exibe a mensagem `"permissão negada"` — simulando o **bloqueio obrigatório** descrito na especificação.

**Exemplo de chmod com bitmask:**
```
chmod 640 relatorio.txt
-> perm = 0640 = 0b110_100_000
-> owner: rw-, group: r--, others: ---
```

---

## Conceitos Teóricos Implementados

### Conceito de Arquivo e seus Atributos
Implementado na struct `Inode`: nome, tipo (enum `FileType`), tamanho, timestamps de criação/modificação/acesso, ID único (inode), permissões.

### Operações com Arquivos
Todas as operações básicas são simuladas internamente na memória:
- **Criar**: `touch` → aloca inode + nó na árvore
- **Escrever**: `write` → divide conteúdo em blocos e grava no disco simulado
- **Ler**: `cat` → percorre os blocos do inode e reconstrói o conteúdo
- **Copiar**: `cp` → lê blocos da origem e aloca novos blocos para o destino
- **Mover/Renomear**: `mv` → altera o campo `name` do inode
- **Excluir**: `rm` → libera blocos, marca inode como inativo, remove da árvore

### FCB e Inode Simulado
A struct `Inode` implementa diretamente o FCB descrito na teoria. O `id` do inode é o seu índice na tabela global, análogo ao número de inode Unix. O `stat` exibe todos os campos do FCB.

### Estrutura de Diretórios em Árvore
`DirNode` forma uma árvore n-ária. O nó raiz (`root_dir`) é criado na inicialização. O ponteiro `current_dir` simula o diretório de trabalho atual. O comando `tree` exibe a hierarquia visualmente.

### Proteção de Acesso (RWX, chmod, bitmasks)
A função `check_permission()` implementa a verificação em três classes (owner/group/others) usando operadores bit a bit. O `chmod` converte o valor octal e atualiza o campo `permissions` do inode via atribuição direta.

### Simulação de Alocação de Blocos
Implementada como **alocação indexada** (similar ao i-node Unix com ponteiros diretos). O `block_bitmap[]` gerencia blocos livres/ocupados. O comando `blocks` demonstra a divisão do conteúdo e o mapeamento lógico→físico.

---

## Comparação com Comandos Linux Reais

| Simulador | Linux Real | Comportamento |
|---|---|---|
| `ls -l` | `ls -l` | Lista arquivos com permissões, inode, tamanho, data |
| `stat nome` | `stat nome` | Exibe FCB/inode completo |
| `chmod 755 arq` | `chmod 755 arq` | Altera bits de permissão por bitmask octal |
| `mkdir dir` | `mkdir dir` | Cria diretório na hierarquia |
| `touch arq` | `touch arq` | Cria arquivo ou atualiza timestamps |
| `write arq "..."` | `echo "..." > arq` | Escreve conteúdo (sobrescreve) |
| `cat arq` | `cat arq` | Lê e exibe conteúdo |
| `cp a b` | `cp a b` | Copia arquivo |
| `mv a b` | `mv a b` | Renomeia/move |
| `rm arq` | `rm arq` | Remove arquivo e libera blocos |
| `su other` | `su usuario` | Troca usuário simulado |
| `diskinfo` | `df -h` | Mostra uso de disco |
| `blocks arq` | `debugfs` / `filefrag` | Mostra mapeamento de blocos |
